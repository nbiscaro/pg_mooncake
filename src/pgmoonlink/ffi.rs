use super::client::{scan_table_begin, scan_table_end};
use pgrx::prelude::*;
use std::ffi::{c_char, CStr};

#[pg_guard]
#[no_mangle]
extern "C-unwind" fn mooncake_scan_table_begin(
    schema: *const c_char,
    table: *const c_char,
    data: *mut *mut u8,
    len: *mut usize,
) {
    let schema = unsafe { CStr::from_ptr(schema).to_str().unwrap().to_owned() };
    let table = unsafe { CStr::from_ptr(table).to_str().unwrap().to_owned() };
    let mut bytes = scan_table_begin(schema, table).unwrap();
    unsafe { *data = bytes.as_mut_ptr() };
    unsafe { *len = bytes.len() };
    std::mem::forget(bytes);
}

#[pg_guard]
#[no_mangle]
extern "C-unwind" fn mooncake_scan_table_end(
    schema: *const c_char,
    table: *const c_char,
    data: *mut u8,
    len: usize,
) {
    let schema = unsafe { CStr::from_ptr(schema).to_str().unwrap().to_owned() };
    let table = unsafe { CStr::from_ptr(table).to_str().unwrap().to_owned() };
    unsafe { Vec::from_raw_parts(data, len, len) };
    let _ = scan_table_end(schema, table);
}
