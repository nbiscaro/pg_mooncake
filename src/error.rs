use bincode::error::{DecodeError, EncodeError};
use std::io::Error as IoError;
use std::num::TryFromIntError;
use std::result;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum Error {
    #[error("decode error: {0}")]
    Decode(#[from] DecodeError),

    #[error("encode error: {0}")]
    Encode(#[from] EncodeError),

    #[error("IO error: {0}")]
    Io(#[from] IoError),

    #[error("conversion error: {0}")]
    TryFromInt(#[from] TryFromIntError),
}

pub type Result<T> = result::Result<T, Error>;
