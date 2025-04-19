use bincode::{config, Decode, Encode};

pub(super) const BINCODE_CONFIG: config::Configuration = config::standard();

pub(super) const SOCKET_PATH: &str = "pg_moonlink.sock";

#[derive(Debug, Eq, PartialEq, Hash)]
pub(super) struct TableId {
    pub database: String,
    pub schema: String,
    pub table: String,
}

#[derive(Debug, Encode, Decode)]
pub(super) enum Request {
    CreateTable {
        schema: String,
        table: String,
        uri: String,
    },
    ScanTableBegin {
        schema: String,
        table: String,
    },
    ScanTableEnd {
        schema: String,
        table: String,
    },
}
