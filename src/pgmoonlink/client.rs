use super::common::*;
use crate::error::Result;
use bincode::{Decode, Encode};
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::sync::{LazyLock, Mutex};

static STREAM: LazyLock<Mutex<UnixStream>> =
    LazyLock::new(|| Mutex::new(UnixStream::connect(SOCKET_PATH).unwrap()));

pub fn create_table(schema: String, table: String, uri: String) -> Result<()> {
    let mut stream = STREAM.lock().unwrap();
    write(&mut stream, &Request::CreateTable { schema, table, uri })?;
    read(&mut stream)
}

pub(super) fn scan_table_begin(schema: String, table: String) -> Result<Vec<u8>> {
    let mut stream = STREAM.lock().unwrap();
    write(&mut stream, &Request::ScanTableBegin { schema, table })?;
    read(&mut stream)
}

pub(super) fn scan_table_end(schema: String, table: String) -> Result<()> {
    let mut stream = STREAM.lock().unwrap();
    write(&mut stream, &Request::ScanTableEnd { schema, table })?;
    read(&mut stream)
}

fn write<E: Encode>(stream: &mut UnixStream, data: &E) -> Result<()> {
    let bytes = bincode::encode_to_vec(data, BINCODE_CONFIG)?;
    let len = u32::try_from(bytes.len())?;
    stream.write_all(&len.to_ne_bytes())?;
    stream.write_all(&bytes)?;
    Ok(())
}

fn read<D: Decode<()>>(stream: &mut UnixStream) -> Result<D> {
    let mut buf = [0; 4];
    stream.read_exact(&mut buf)?;
    let len = u32::from_ne_bytes(buf);
    let mut bytes = vec![0; len as usize];
    stream.read_exact(&mut bytes)?;
    Ok(bincode::decode_from_slice(&bytes, BINCODE_CONFIG)?.0)
}
