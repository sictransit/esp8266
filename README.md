# esp8266

## Mosquito firmware

Before compiling the sketch, copy `mosquito/secrets.example.h` to
`mosquito/secrets.h` and set a unique access-point name and password. The local
`secrets.h` is ignored by Git. ESP8266 access-point passwords must be at least
8 characters.

The password is compiled into the firmware and can be recovered from it, so do
not reuse a password used for other services. If the previous hardcoded
password was used, change it on the device; it may also remain in Git history.
