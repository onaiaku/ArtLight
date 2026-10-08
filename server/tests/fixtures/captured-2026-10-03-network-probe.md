# Captured usbip behaviour — z13 (importer candidate), 2026-10-03

Captured from `onaiaku@10.6.0.3` (Niks-z13, CachyOS). Saved because these are the facts the
importer's probe and parser have to be written against, and three of them are not what I assumed.

## 1. An unreachable exporter does not fail — it HANGS, silently

```
$ usbip list -r 192.168.50.35
<no output, no error, still running after 20s>          exit code 124 (killed by timeout)
  stdout: 0 bytes
  stderr: 0 bytes
```

The z13 is on `wlan0 192.168.1.105/24`; the exporter (mini PC) is on `192.168.50.35`. Different
subnet, no route:

```
  NO ROUTE : nc -z -w3 192.168.50.35 3240
  NO ROUTE : nc -z -w3 10.6.0.6 3240
```

**So: "exporter asleep" and "exporter offering nothing" are indistinguishable without a timeout.**
One returns an empty list immediately; the other never returns at all. Any call on the attach path
must carry its own timeout, and the two states must be reported differently — an attach path that
blocks forever on a sleeping exporter is worse than one that reports a reason.

## 2. `usbip port` needs `vhci_hcd`, and `usbip` on PATH proves nothing

```
$ usbip port
libusbip: error: udev_device_new_from_subsystem_sysname failed
usbip: error: open vhci_driver (is vhci_hcd loaded?)
usbip: error: list imported devices
```

```
$ lsmod | grep -E 'vhci|usbip'
  usbip_host             49152  0
  usbip_core             45056  1 usbip_host
```

`usbip` (usbip-utils 2.0) is installed here, and this machine still cannot import a single device —
`vhci_hcd` is not loaded. **The importer probe must check the module, not the binary.** And since
ports only exist under `vhci_hcd`, the whole `busid → port` map is empty until it is loaded.

## 3. Local list — real row shape (captured)

```
 - busid 3-10 (8087:0033)
   Intel Corp. : AX211 Bluetooth (8087:0033)

 - busid 3-6 (0b05:1a30)
   ASUSTek Computer, Inc. : unknown product (0b05:1a30)

 - busid 3-7 (04f3:0c6e)
   Elan Microelectronics Corp. : unknown product (04f3:0c6e)

 - busid 3-8 (13d3:5492)
   IMC Networks : unknown product (13d3:5492)
```

`busid` is space-separated here; the two lines per device are the row. Note the description is
`vendor : product` with the `vid:pid` repeated at the end, and `unknown product` is a normal value,
not a parse failure.

## 4. NOT yet captured — the remote list format

`usbip list -r <exporter>` produced no bytes, because the two machines are on different networks
right now (the z13 is not at home). **The remote format is therefore unverified.** Capture it on the
same LAN before trusting the remote parser: `usbip list -r <exporter> > remote-list.txt`.
