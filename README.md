# joycmd
Map joystick button combinations to shell commands


## Compile:
```bash
gcc joystick.c -o joycmd
```

## Run:
```bash
./joycmd [-d] [/dev/input/jsX]
```
- **-d:** Debug mode (optional)
- **Device:** Location of the connected joystick (optional)

## Configs:
```bash
nano /etc/joycmd/joycmd.conf
```