use super::common::*;
use super::table_metadata::TableMetadata;
use crate::error::Result;
use bincode::{Decode, Encode};
use moonlink_backend::MoonlinkBackend;
use std::sync::LazyLock;
use tokio::fs;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{UnixListener, UnixStream};
use tokio::signal::unix::{signal, SignalKind};

static BACKEND: LazyLock<MoonlinkBackend<TableId>> = LazyLock::new(|| MoonlinkBackend::new());

#[tokio::main]
pub(super) async fn start() -> Result<()> {
    let mut sigterm = signal(SignalKind::terminate())?;
    LazyLock::force(&BACKEND);
    if fs::metadata(SOCKET_PATH).await.is_ok() {
        fs::remove_file(SOCKET_PATH).await?;
    }
    let listener = UnixListener::bind(SOCKET_PATH)?;
    loop {
        tokio::select! {
            _ = sigterm.recv() => break,
            Ok((stream, _addr)) = listener.accept() => {
                tokio::spawn(async move { handle_stream(stream).await });
            }
        }
    }
    Ok(())
}

async fn handle_stream(mut stream: UnixStream) -> Result<()> {
    loop {
        match read(&mut stream).await? {
            Request::CreateTable {
                table_id,
                schema,
                table,
            } => create_table(&mut stream, table_id, schema, table).await?,
            Request::ScanTableBegin { table_id } => scan_table_begin(&mut stream, table_id).await?,
            Request::ScanTableEnd { table_id } => scan_table_end(&mut stream, table_id).await?,
        }
    }
}

async fn create_table(
    stream: &mut UnixStream,
    table_id: TableId,
    schema: String,
    table: String,
) -> Result<()> {
    BACKEND
        .create_table(
            table_id,
            "localhost",
            28817,
            "vscode",
            "password", // TODO
            "pg_mooncake",
            &schema,
            &table,
        )
        .await?;
    write(stream, &()).await
}

async fn scan_table_begin(stream: &mut UnixStream, table_id: TableId) -> Result<()> {
    let (data_files, position_deletes) = BACKEND.scan_table(table_id).await?; // TODO
    let metadata = TableMetadata {
        data_files,
        position_deletes,
    };
    let bytes = bincode::encode_to_vec(metadata, BINCODE_CONFIG)?;
    write(stream, &bytes).await
}

async fn scan_table_end(stream: &mut UnixStream, _table_id: TableId) -> Result<()> {
    write(stream, &()).await
}

async fn write<E: Encode>(stream: &mut UnixStream, data: &E) -> Result<()> {
    let bytes = bincode::encode_to_vec(data, BINCODE_CONFIG)?;
    let len = u32::try_from(bytes.len())?;
    stream.write_all(&len.to_ne_bytes()).await?;
    stream.write_all(&bytes).await?;
    Ok(())
}

async fn read<D: Decode<()>>(stream: &mut UnixStream) -> Result<D> {
    let mut buf = [0; 4];
    stream.read_exact(&mut buf).await?;
    let len = u32::from_ne_bytes(buf);
    let mut bytes = vec![0; len as usize];
    stream.read_exact(&mut bytes).await?;
    Ok(bincode::decode_from_slice(&bytes, BINCODE_CONFIG)?.0)
}
