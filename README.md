# esp8266

## Mosquito firmware

Before compiling the sketch, copy `mosquito/secrets.example.h` to
`mosquito/secrets.h` and set a unique access-point name and password. The local
`secrets.h` is ignored by Git. ESP8266 access-point passwords must be at least
8 characters.

The password is compiled into the firmware and can be recovered from it, so do
not reuse a password used for other services. If the previous hardcoded
password was used, change it on the device; it may also remain in Git history.

## Pinger firmware

Before compiling the sketch, copy `pinger/secrets.example.h` to
`pinger/secrets.h` and set the Wi-Fi network name and password. The local
`secrets.h` is ignored by Git. These credentials are compiled into the firmware,
so use a dedicated network password. If the previously hardcoded password is
active, rotate it; it may also remain in Git history.
