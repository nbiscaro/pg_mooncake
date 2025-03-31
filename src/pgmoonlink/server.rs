use super::common::*;
use super::table_metadata::TableMetadata;
use crate::error::Result;
use bincode::{Decode, Encode};
use tokio::fs;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{UnixListener, UnixStream};
use tokio::signal::unix::{signal, SignalKind};

#[tokio::main]
pub(super) async fn start() -> Result<()> {
    let mut sigterm = signal(SignalKind::terminate())?;
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
            Request::ScanBegin { schema, table } => scan_begin(&mut stream, schema, table).await?,
            Request::ScanEnd { schema, table } => scan_end(&mut stream, schema, table).await?,
        }
    }
}

async fn scan_begin(stream: &mut UnixStream, _schame: String, _table: String) -> Result<()> {
    let metadata = TableMetadata {
        data_files: vec![
            "/home/vscode/1.parquet".to_owned(),
            "/home/vscode/2.parquet".to_owned(),
        ],
        position_deletes: vec![(1, 0)],
    };
    let bytes = bincode::encode_to_vec(metadata, BINCODE_CONFIG)?;
    write(stream, &bytes).await
}

async fn scan_end(stream: &mut UnixStream, _schame: String, _table: String) -> Result<()> {
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
