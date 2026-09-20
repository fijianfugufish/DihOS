# Local Lenovo 83ED GPU zap

`qcdxkmsuc8380.mbn` is copied locally from the active Windows Adreno driver
31.0.128.0 (2025-10-16), installed INF `oem354.inf`. The installed INF matches
`qcdx8380.inf_arm64_92a77a104a5379f7/qcdx8380.inf` byte-for-byte.

Expected local file size: 12088 bytes.
SHA-256: `8DB5FFEEFC566620A9BC77D513677C20EAA782030485341E7ED1CECD5E35D1B2`.

The proprietary binary is git-ignored; no redistribution licence is assumed.
It is not interchangeable with generic reference-board zap firmware, nor
necessarily with firmware for another OEM device. Secure firmware still
authenticates it; DihOS does not bypass signature checking. The source Windows
installation is unchanged. The generic upstream file is retained separately.

Revision 47 reached PAS INIT_IMAGE without a CPU exception but returned
`x0=0xFFCFFFBB`. OEM firmware selection is a candidate correction, not a
confirmed decoding of that firmware-specific error. Hardware testing is required.
