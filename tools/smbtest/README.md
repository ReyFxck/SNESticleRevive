# SMB frontend regression

Run `python3 tools/smbtest/check_smb.py` with GCC available. It compiles the
production frontend functions with filesystem, module and RPC fixtures
under ASan/UBSan. The copied `ps2smb.h` retains the SDK license/ABI; assertions
check the logon and open-share structures and request lengths.

Coverage includes parsing and integer overflow, configuration discovery on
mass9/mc7, MX4SIO-only driver loading, saving the existing configuration,
password hashing, logon/share order, avoiding redundant mount RPCs,
disconnect, error reporting and recovery. Temporary files are isolated.

This checks the frontend flow, not TCP, the IOP SMB implementation, a real
server or PS2 playback. The current SDK module speaks SMB1/NT1. SMB2/3
compatibility is not implemented by these fixes.
