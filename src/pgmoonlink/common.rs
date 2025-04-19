use bincode::{config, Decode, Encode};

pub(super) const BINCODE_CONFIG: config::Configuration = config::standard();

pub(super) const SOCKET_PATH: &str = "pg_moonlink.sock";

#[derive(Debug, Eq, PartialEq, Hash, Encode, Decode)]
pub(super) struct TableId {
    pub database_id: u32,
    pub table_id: u32,
}

#[derive(Debug, Encode, Decode)]
pub(super) enum Request {
    CreateTable {
        table_id: TableId,
        schema: String,
        table: String,
    },
    ScanTableBegin {
        table_id: TableId,
    },
    ScanTableEnd {
        table_id: TableId,
    },
}
