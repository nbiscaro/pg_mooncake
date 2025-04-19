use std::result;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum Error {
    #[error("decode error: {0}")]
    Decode(#[from] bincode::error::DecodeError),

    #[error("encode error: {0}")]
    Encode(#[from] bincode::error::EncodeError),

    #[error("format error: {0}")]
    FormatError(#[from] std::fmt::Error),

    #[error("internal error: {0}")]
    Internal(String),

    #[error("invalid input: {0}")]
    Invalid(String),

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),

    #[error("moonlink error: {0}")]
    Moonlink(#[from] moonlink_backend::Error),

    #[error("postgres error: {0}")]
    Postgres(#[from] postgres::Error),

    #[error("conversion error: {0}")]
    TryFromInt(#[from] std::num::TryFromIntError),
}

pub type Result<T> = result::Result<T, Error>;
