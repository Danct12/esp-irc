# IRC Admin Check Example

This example application implements a basic admin check based on user's hostname.

## Configuration
- Run `idf.py menuconfig` in this directory
- Navigate to `Example Configuration` and fill in the details.

- If your IRC network requires TLS connection:
  - Enable TLS support in `Component config -> ESPIRC Settings`
  - Enable `SSL/TLS Connection` in `Example Configuration -> IRC Settings`

- Build and flash the example

## Demo

### Client
```
<@danct12> !admin
<@espircadmin> danct12: Access Granted.
<@JAA> !admin
<@espircadmin> JAA: Access Denied.
```

### ESP32 Output:
```
I (19360) IRC_AdminCheck: GRANTED - danct12 (hackint/user/danct12)
I (45350) IRC_AdminCheck: DENIED - JAA (archiveteam/JAA)
```
