//! HeartGold/SoulSilver ROM parsers for Following Pokemon, called from C through
//! ffi.rs. Like the rest of src/hgss there is no allocation, and outside tests no
//! std. Every input is untrusted: a malformed ROM makes a parser return failure,
//! never panic, since a panic aborts the emulator.
#![cfg_attr(not(test), no_std)]

mod bytes;
pub mod msg;

#[cfg(not(test))]
mod ffi;
#[cfg(test)]
mod test_util;

#[cfg(not(test))]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    extern "C" {
        fn abort() -> !;
    }
    unsafe { abort() }
}

// panic = "abort" never unwinds, but the prebuilt libcore still names this
// symbol and a debug-profile link fails without it.
#[cfg(not(test))]
#[no_mangle]
pub extern "C" fn rust_eh_personality() {}
