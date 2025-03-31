use bincode::{config, Decode, Encode};

pub(super) const BINCODE_CONFIG: config::Configuration = config::standard();

pub(super) const SOCKET_PATH: &str = "pg_moonlink.sock";

#[derive(Debug, Encode, Decode)]
pub(super) enum Request {
    ScanBegin { schema: String, table: String },
    ScanEnd { schema: String, table: String },
}
