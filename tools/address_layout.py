"""Validated identity-address layout shared by Android build tools."""
CANONICAL_GUEST_BASE = 0x7000000000
ANDROID_GUEST_BASE = 0x1000000000
GUEST_WINDOW_SIZE = 0x320000000
SDK_OFFSET = 0x4000000


def guest_base(value):
    """A full window must be 16 KiB aligned and fit the guest's 39-bit VA."""
    base = int(value, 0) if isinstance(value, str) else int(value)
    if base <= 0 or base & 0x3fff or base + GUEST_WINDOW_SIZE > 1 << 39:
        raise ValueError('guest base must be positive, 16 KiB aligned, and leave the whole window below 2^39')
    return base
