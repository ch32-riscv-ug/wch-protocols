use ch32rv_dmi::{DebugModule, DtmAccess, RegName};
use ch32rv_wchlink::{WchLink, Speed};
use std::{path::Path, time::Duration};

fn snapshot(link: &mut WchLink, label: &str) {
    println!("SNAPSHOT {label}");
    for addr in [0x10, 0x11, 0x12, 0x16] {
        println!("DMI {addr:02x} {:?}", link.dmi_read(addr));
    }
    for csr in [0xf14, 0xf12, 0xf13, 0x301, 0x7b1] {
        println!("CSR {csr:03x} {:?}", DebugModule::new(link).read_reg(RegName::Csr(csr)));
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let capture = std::env::args().nth(1).expect("capture path");
    ch32rv_usb::capture::start(Path::new(&capture))?;
    let _lock = ch32rv_usb::DeviceLock::acquire("49808F06CE30", Duration::from_secs(10))?;
    let dev = ch32rv_usb::enumerate()?.into_iter()
        .find(|d| d.serial() == Some("49808F06CE30")).ok_or("probe absent")?;
    let mut link = WchLink::open(&dev)?;
    link.detach_chip()?;
    println!("PROBE {:?}", link.probe_info()?);
    link.set_speed_default(Speed::Low)?;
    let attach = link.attach_chip()?;
    println!("ATTACH {attach:?}");
    if attach.chip_id != 0x4170053d { link.detach_chip()?; return Err("unexpected target".into()); }
    let result = (|| -> Result<(), Box<dyn std::error::Error>> {
        DebugModule::new(&mut link).halt()?;
        snapshot(&mut link, "baseline");
        // Standard RISC-V DMCONTROL hartsel[9:0] occupies bits 25:16.
        // No reset, flash, option, or general-purpose memory writes.
        for hart in [1u32, 0, 1, 0] {
            let select = 1 | (hart << 16);
            println!("SELECT hart={hart} dmcontrol={select:08x}");
            link.dmi_write(0x10, select)?;
            println!("SELECT_READBACK {:?}", link.dmi_read(0x10));
            let status = link.dmi_read(0x11)?;
            println!("SELECT_STATUS {status:08x}");
            if status & 0xc000 != 0 { println!("NONEXISTENT"); continue; }
            if status & 0x300 != 0x300 {
                link.dmi_write(0x10, select | 0x80000000)?;
                for _ in 0..16 { if link.dmi_read(0x11)? & 0x300 == 0x300 { break; } }
                link.dmi_write(0x10, select)?;
            }
            snapshot(&mut link, &format!("hartsel-{hart}"));
        }
        Ok(())
    })();
    // Restore core-0 selection even if a diagnostic read fails.
    println!("RESTORE {:?}", link.dmi_write(0x10, 1));
    println!("DETACH {:?}", link.detach_chip());
    result
}
