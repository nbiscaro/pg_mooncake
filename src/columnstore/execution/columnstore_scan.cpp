#include "columnstore/columnstore_metadata.hpp"
#include "columnstore/columnstore_statistics.hpp"
#include "columnstore/columnstore_table.hpp"
#include "duckdb/catalog/catalog_entry/table_function_catalog_entry.hpp"
#include "duckdb/common/multi_file_reader.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"

extern "C" {
#include "postgres.h"
#include "utils/snapmgr.h"
}

namespace duckdb {

struct ColumnstoreScanMultiFileReaderGlobalState : public MultiFileReaderGlobalState {
    ColumnstoreScanMultiFileReaderGlobalState(vector<LogicalType> extra_columns,
                                              optional_ptr<const MultiFileList> file_list)
        : MultiFileReaderGlobalState(extra_columns, file_list) {}

    idx_t row_id_index = DConstants::INVALID_INDEX;
    idx_t file_row_number_index = DConstants::INVALID_INDEX;
    unique_ptr<Vector> row_ids;
};

struct ColumnstoreScanMultiFileReader : public MultiFileReader {
    static unique_ptr<MultiFileReader> Create() {
        return std::move(make_uniq<ColumnstoreScanMultiFileReader>());
    }

    unique_ptr<MultiFileList> DynamicFilterPushdown(ClientContext &context, const MultiFileList &files,
                                                    const MultiFileReaderOptions &options, const vector<string> &names,
                                                    const vector<LogicalType> &types,
                                                    const vector<column_t> &column_ids,
                                                    TableFilterSet &filters) override {
        const vector<string> file_paths = files.GetPaths();
        vector<string> filtered_file_paths;
        for (idx_t file_number = 0; file_number < file_paths.size(); file_number++) {
            auto &file_path = file_paths[file_number];
            auto file_stats = columnstore_stats.Get<DataFileStatistics>(StringUtil::GetFileName(file_path));
            auto skip_file = [&](auto &entry) {
                auto stats = file_stats->Get(names[column_ids[entry.first]]);
                return stats && entry.second->CheckStatistics(*stats) == FilterPropagateResult::FILTER_ALWAYS_FALSE;
            };
            if (!any_of(filters.filters.begin(), filters.filters.end(), skip_file)) {
                filtered_file_paths.push_back(file_path);
                file_numbers.push_back(file_number);
            }
        }
        return filtered_file_paths.size() < file_paths.size() ? make_uniq<SimpleMultiFileList>(filtered_file_paths)
                                                              : nullptr;
    }

    unique_ptr<MultiFileReaderGlobalState>
    InitializeGlobalState(ClientContext &context, const MultiFileReaderOptions &file_options,
                          const MultiFileReaderBindData &bind_data, const MultiFileList &file_list,
                          const vector<LogicalType> &global_types, const vector<string> &global_names,
                          const vector<column_t> &global_column_ids) override {

        bool include_file_row_number = false;
        auto it = find(global_names.begin(), global_names.end(), "file_row_number");
        if (it != global_names.end()) {
            include_file_row_number = true;
        }

        vector<LogicalType> extra_columns;
        if (include_file_row_number) {
            extra_columns.push_back(LogicalType::BIGINT);
        }

        auto global_state = make_uniq<ColumnstoreScanMultiFileReaderGlobalState>(std::move(extra_columns), file_list);

        if (include_file_row_number) {
            auto it = std::find_if(global_column_ids.begin(), global_column_ids.end(), IsRowIdColumnId);
            if (it != global_column_ids.end()) {
                global_state->row_id_index = NumericCast<idx_t>(std::distance(global_column_ids.begin(), it));
                global_state->row_ids = make_uniq<Vector>(LogicalType::BIGINT);
            }
            global_state->file_row_number_index = global_column_ids.size();
        }
        return std::move(global_state);
    }

    void CreateMapping(const string &file_name, const vector<LogicalType> &local_types,
                       const vector<string> &local_names, const vector<LogicalType> &global_types,
                       const vector<string> &global_names, const vector<column_t> &global_column_ids,
                       optional_ptr<TableFilterSet> filters, MultiFileReaderData &reader_data,
                       const string &initial_file, const MultiFileReaderBindData &options,
                       optional_ptr<MultiFileReaderGlobalState> global_state) override {
        MultiFileReader::CreateMapping(file_name, local_types, local_names, global_types, global_names,
                                       global_column_ids, filters, reader_data, initial_file, options, global_state);
        auto &gstate = global_state->Cast<ColumnstoreScanMultiFileReaderGlobalState>();
        if (gstate.file_row_number_index != DConstants::INVALID_INDEX) {
            auto it = std::find_if(local_names.begin(), local_names.end(), [](const string &local_name) {
                return StringUtil::CIEquals(local_name, "file_row_number");
            });
            if (it != local_names.end()) {
                reader_data.column_mapping.push_back(gstate.file_row_number_index);
                reader_data.column_ids.push_back(NumericCast<idx_t>(std::distance(local_names.begin(), it)));
            }
        }
    }

    void ApplyDeletions(const MultiFileReaderData &reader_data, ColumnstoreScanMultiFileReaderGlobalState &gstate,
                        DataChunk &chunk) {
        auto &file_row_numbers = chunk.data[gstate.file_row_number_index];
        file_row_numbers.Flatten(chunk.size());
        auto file_row_numbers_data = FlatVector::GetData<int64_t>(file_row_numbers);

        SelectionVector sel(STANDARD_VECTOR_SIZE);
        idx_t sel_size = 0;

        const idx_t file_list_idx = reader_data.file_list_idx.GetIndex();
        const auto &file_paths = gstate.file_list->GetPaths();
        D_ASSERT(file_list_idx < file_paths.size());
        const auto &file_name = file_paths[file_list_idx];

        unordered_map<idx_t, vector<idx_t>> chunk_to_rows;
        for (idx_t i = 0; i < chunk.size(); i++) {
            uint64_t offset_64 = file_row_numbers_data[i];
            uint32_t offset_in_file = NumericCast<uint32_t>(offset_64);

            idx_t chunk_idx = offset_in_file / STANDARD_VECTOR_SIZE;
            chunk_to_rows[chunk_idx].push_back(i);
        }

        Snapshot snapshot = GetActiveSnapshot();
        DVManager dv_manager(snapshot);

        for (auto &kv : chunk_to_rows) {
            idx_t chunk_idx = kv.first;
            auto &row_indices = kv.second;

            DeletionVector dv = dv_manager.FetchDV(file_name, chunk_idx);

            for (auto row_i : row_indices) {
                uint64_t offset_64 = file_row_numbers_data[row_i];
                uint32_t offset_in_file = NumericCast<uint32_t>(offset_64);

                idx_t offset_in_chunk = offset_in_file % STANDARD_VECTOR_SIZE;
                if (!dv.IsDeleted(offset_in_chunk)) {
                    sel.set_index(sel_size++, row_i);
                }
            }
        }

        chunk.Slice(sel, sel_size);
    }

    void FinalizeChunk(ClientContext &context, const MultiFileReaderBindData &bind_data,
                       const MultiFileReaderData &reader_data, DataChunk &chunk,
                       optional_ptr<MultiFileReaderGlobalState> global_state) override {
        MultiFileReader::FinalizeChunk(context, bind_data, reader_data, chunk, global_state);
        auto &gstate = global_state->Cast<ColumnstoreScanMultiFileReaderGlobalState>();
        const bool has_file_row_number = (gstate.file_row_number_index != DConstants::INVALID_INDEX &&
                                          gstate.file_row_number_index < chunk.data.size());

        if (has_file_row_number) {
            ApplyDeletions(reader_data, gstate, chunk);
        }

        if (gstate.row_id_index != DConstants::INVALID_INDEX) {
            auto &file_row_numbers = chunk.data[gstate.file_row_number_index];
            file_row_numbers.Flatten(chunk.size());
            auto file_row_numbers_data = FlatVector::GetData<int64_t>(file_row_numbers);
            gstate.row_ids->SetVectorType(VectorType::FLAT_VECTOR);
            auto row_ids_data = FlatVector::GetData<row_t>(*gstate.row_ids);
            const idx_t file_list_idx = NumericCast<int32_t>(reader_data.file_list_idx.GetIndex());
            const idx_t file_number = file_numbers.empty() ? file_list_idx : file_numbers[file_list_idx];
            for (idx_t i = 0; i < chunk.size(); i++) {
                row_ids_data[i] = (file_number << 32) + NumericCast<uint32_t>(file_row_numbers_data[i]);
            }
            chunk.data[gstate.row_id_index].Reference(*gstate.row_ids);
        }
    }

    vector<idx_t> file_numbers;
};

void EmptyColumnstoreScan(ClientContext &context, TableFunctionInput &data, DataChunk &output) {}

TableFunction GetParquetScan(ClientContext &context) {
    return ExtensionUtil::GetTableFunction(*context.db, "parquet_scan")
        .functions.GetFunctionByArguments(context, {LogicalType::LIST(LogicalType::VARCHAR)});
}

unique_ptr<GlobalTableFunctionState> ColumnstoreScanInitGlobal(ClientContext &context, TableFunctionInitInput &input) {
    // UPDATE can generate duplicate global_column_ids which ParquetReader doesn't expect
    unordered_map<column_t, idx_t> column_ids_map;
    vector<column_t> column_ids;
    for (idx_t i = 0; i < input.column_ids.size(); i++) {
        if (column_ids_map.count(input.column_ids[i]) == 0) {
            column_ids_map[input.column_ids[i]] = column_ids.size();
            column_ids.push_back(input.column_ids[i]);
        }
    }
    vector<idx_t> projection_ids(input.projection_ids);
    for (idx_t i = 0; i < projection_ids.size(); i++) {
        projection_ids[i] = column_ids_map[input.column_ids[projection_ids[i]]];
    }

    TableFunctionInitInput new_input(input.bind_data, column_ids, projection_ids, input.filters);
    return GetParquetScan(context).init_global(context, new_input);
}

TableFunction ColumnstoreTable::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) {
    auto file_names = metadata->DataFilesSearch(oid, &context, &path, &columns);
    auto file_paths = GetFilePaths(path, file_names);
    if (file_paths.empty()) {
        return TableFunction("columnstore_scan", {} /*arguments*/, EmptyColumnstoreScan);
    }

    TableFunction columnstore_scan = GetParquetScan(context);
    columnstore_scan.name = "columnstore_scan";
    columnstore_scan.init_global = ColumnstoreScanInitGlobal;
    columnstore_scan.statistics = nullptr;
    columnstore_scan.get_multi_file_reader = ColumnstoreScanMultiFileReader::Create;

    vector<Value> values;
    for (auto &file_path : file_paths) {
        values.push_back(Value(file_path));
    }
    vector<Value> inputs;
    inputs.push_back(Value::LIST(values));
    named_parameter_map_t named_parameters{{"file_row_number", Value(true)}};
    vector<LogicalType> input_table_types;
    vector<string> input_table_names;
    TableFunctionBindInput bind_input(inputs, named_parameters, input_table_types, input_table_names, nullptr /*info*/,
                                      nullptr /*binder*/, columnstore_scan, {} /*ref*/);
    vector<LogicalType> return_types;
    vector<string> names;
    bind_data = columnstore_scan.bind(context, bind_input, return_types, names);
    return columnstore_scan;
}

} // namespace duckdb
