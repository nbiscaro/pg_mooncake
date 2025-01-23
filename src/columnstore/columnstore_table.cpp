#include "columnstore/columnstore_table.hpp"
#include "columnstore/columnstore_deletion_vector.hpp"
#include "columnstore/columnstore_metadata.hpp"
#include "duckdb/common/serializer/memory_stream.hpp"
#include "duckdb/common/types/uuid.hpp"
#include "lake/lake.hpp"
#include "parquet_reader.hpp"
#include "parquet_writer.hpp"
#include "pgmooncake_guc.hpp"

extern "C" {
#include "postgres.h"
#include "utils/snapmgr.h"
}

namespace duckdb {

class SingleFileCachedWriteFileSystem : public FileSystem {
public:
    SingleFileCachedWriteFileSystem(ClientContext &context, const string &file_name)
        : fs(GetFileSystem(context)), cached_file_path(x_mooncake_local_cache + file_name), stream(nullptr) {}

public:
    unique_ptr<FileHandle> OpenFile(const string &path, FileOpenFlags flags,
                                    optional_ptr<FileOpener> opener = nullptr) override {
        if (IsRemoteFile(path) && mooncake_enable_local_cache) {
            auto disk_space = fs.GetAvailableDiskSpace(x_mooncake_local_cache);
            if (disk_space.IsValid() && disk_space.GetIndex() > x_min_disk_space) {
                cached_file = fs.OpenFile(cached_file_path, flags, opener);
            }
        }
        return fs.OpenFile(path, flags, opener);
    }

    int64_t Write(FileHandle &handle, void *buffer, int64_t nr_bytes) override {
        if (stream) {
            stream->WriteData(data_ptr_cast(buffer), nr_bytes);
        }
        if (cached_file) {
            int64_t bytes_written = fs.Write(*cached_file, buffer, nr_bytes);
            D_ASSERT(bytes_written == nr_bytes);
        }
        return fs.Write(handle, buffer, nr_bytes);
    }

    string GetName() const override {
        return "SingleFileCachedWriteFileSystem";
    }

    void StartRecording(MemoryStream &stream) {
        this->stream = &stream;
    }

private:
    static const idx_t x_min_disk_space = 1024 * 1024 * 1024;

    FileSystem &fs;
    string cached_file_path;
    unique_ptr<FileHandle> cached_file;
    MemoryStream *stream;
};

class DataFileWriter {
public:
    DataFileWriter(ClientContext &context, string path, string file_name, vector<LogicalType> types,
                   vector<string> names, ChildFieldIDs field_ids)
        : fs(context, file_name), collection(context, types, ColumnDataAllocatorType::HYBRID),
          writer(context, fs, path + file_name, std::move(types), std::move(names),
                 duckdb_parquet::format::CompressionCodec::SNAPPY /*codec*/, std::move(field_ids), {} /*kv_metadata*/,
                 {} /*encryption_config*/, 1.0 /*dictionary_compression_ratio_threshold*/, {} /*compression_level*/,
                 true /*debug_use_openssl*/) {
        collection.InitializeAppend(append_state);
    }

public:
    // Return true if needs to rotate to a new data file
    bool Write(DataChunk &chunk) {
        collection.Append(append_state, chunk);
        if (collection.Count() >= x_row_group_size || collection.SizeInBytes() >= x_row_group_size_bytes) {
            writer.Flush(collection);
            append_state.current_chunk_state.handles.clear();
            collection.InitializeAppend(append_state);
            return writer.FileSize() >= x_file_size_bytes;
        }
        return false;
    }

    std::tuple<idx_t, string_t> Finalize() {
        writer.Flush(collection);
        idx_t offset = writer.GetWriter().offset;
        idx_t total_written = writer.GetWriter().total_written;
        fs.StartRecording(stream);
        writer.Finalize();
        idx_t file_size = total_written + stream.GetPosition();
        string_t file_metadata{const_char_ptr_cast(stream.GetData()) + offset,
                               NumericCast<uint32_t>(stream.GetPosition() - offset - 8)};
        return {file_size, std::move(file_metadata)};
    }

private:
    static const idx_t x_row_group_size = duckdb::Storage::ROW_GROUP_SIZE;
    static const idx_t x_row_group_size_bytes = x_row_group_size * 1024;
    static const idx_t x_file_size_bytes = 1 << 30;

    SingleFileCachedWriteFileSystem fs;
    ColumnDataCollection collection;
    ColumnDataAppendState append_state;
    ParquetWriter writer;
    MemoryStream stream;
};

class ColumnstoreWriter {
public:
    ColumnstoreWriter(Oid oid, ColumnstoreMetadata &metadata, string path, vector<LogicalType> types,
                      vector<string> names)
        : oid(oid), metadata(metadata), path(std::move(path)), types(std::move(types)), names(std::move(names)) {}

public:
    void Write(ClientContext &context, DataChunk &chunk) {
        if (!writer) {
            file_name = UUID::ToString(UUID::GenerateRandomUUID()) + ".parquet";
            ChildFieldIDs field_ids;
            for (idx_t i = 0; i < names.size(); i++) {
                (*field_ids.ids)[names[i]] = FieldID(i);
            }
            writer = make_uniq<DataFileWriter>(context, path, file_name, types, names, std::move(field_ids));
        }
        if (writer->Write(chunk)) {
            FinalizeDataFile();
        }
    }

    void Finalize() {
        if (writer) {
            FinalizeDataFile();
        }
    }

private:
    void FinalizeDataFile() {
        auto [file_size, file_metadata] = writer->Finalize();
        metadata.DataFilesInsert(oid, file_name, file_metadata);
        writer.reset();
        LakeAddFile(oid, file_name, file_size);
    }

private:
    Oid oid;
    ColumnstoreMetadata &metadata;
    string path;
    string file_name;
    vector<LogicalType> types;
    vector<string> names;
    unique_ptr<DataFileWriter> writer;
};

ColumnstoreTable::ColumnstoreTable(Catalog &catalog, SchemaCatalogEntry &schema, CreateTableInfo &info, Oid oid,
                                   Snapshot snapshot)
    : TableCatalogEntry(catalog, schema, info), oid(oid), metadata(make_uniq<ColumnstoreMetadata>(snapshot)),
      path(std::get<0>(metadata->TablesSearch(oid))) {}

ColumnstoreTable::~ColumnstoreTable() = default;

// HACK: force update_is_del_and_insert
TableStorageInfo ColumnstoreTable::GetStorageInfo(ClientContext &context) {
    IndexInfo index_info;
    for (column_t i = 0; i < columns.LogicalColumnCount(); i++) {
        index_info.column_set.insert(i);
    }
    TableStorageInfo result;
    result.index_info.push_back(std::move(index_info));
    return result;
}

void ColumnstoreTable::Insert(ClientContext &context, DataChunk &chunk) {
    if (!writer) {
        writer = make_uniq<ColumnstoreWriter>(oid, *metadata, path, columns.GetColumnTypes(), columns.GetColumnNames());
    }
    writer->Write(context, chunk);
}

void ColumnstoreTable::FinalizeInsert() {
    if (writer) {
        writer->Finalize();
        writer.reset();
    }
}

void ColumnstoreTable::Delete(ClientContext &context, unordered_set<row_t> &row_ids_set,
                              ColumnDataCollection *return_collection) {
    vector<row_t> row_ids(row_ids_set.begin(), row_ids_set.end());
    sort(row_ids.begin(), row_ids.end());

    auto file_names = metadata->DataFilesSearch(oid, &context, &path, &columns);
    auto file_paths = GetFilePaths(path, file_names);
    auto file_chunk_dv_map = DVManager::BuildFileChunkDVs(row_ids);

    Snapshot snapshot = ColumnstoreMetadata::GetActiveSnapshot();
    DVManager dv_manager(snapshot);
    dv_manager.ApplyDeletionVectors(file_chunk_dv_map, file_paths);

    if (return_collection) {
        for (auto &[file_number, chunk_map] : file_chunk_dv_map) {
            auto &file_path = file_paths[file_number];
            ParquetReader reader(context, file_path, ParquetOptions{});

            auto col_count = reader.GetTypes().size();
            for (idx_t column_idx = 0; column_idx < col_count; column_idx++) {
                reader.reader_data.column_mapping.push_back(column_idx);
                reader.reader_data.column_ids.push_back(column_idx);
            }

            DVManager::ReadAndAppendDeletedRows(context, reader, chunk_map, columns, return_collection);
        }
    }

    dv_manager.Flush();
}

vector<string> ColumnstoreTable::GetFilePaths(const string &path, const vector<string> &file_names) {
    vector<string> file_paths;
    if (mooncake_enable_local_cache && FileSystem::IsRemoteFile(path)) {
        auto local_fs = FileSystem::CreateLocal();
        for (auto &file_name : file_names) {
            string cached_file_path = x_mooncake_local_cache + file_name;
            if (local_fs->FileExists(cached_file_path)) {
                file_paths.push_back(cached_file_path);
            } else {
                file_paths.push_back(path + file_name);
            }
        }
    } else {
        for (auto &file_name : file_names) {
            file_paths.push_back(path + file_name);
        }
    }
    return file_paths;
}

} // namespace duckdb
