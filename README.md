# joycmd
Map joystick button combinations to shell commands


## Compile:
```bash
gcc joycmd.c -o joycmd
```

## Run:
```bash
./joycmd [-d -h] [/dev/input/jsX]
```
> JoyCmd creates a configuration file at /etc/joyce/joyce.conf. To generate this file, run JoyCmd as root the first time you use it.

- **-h:** Help
- **-d:** Debug mode (optional)
- **Device:** Location of the connected joystick (optional)

## Configs:
```bash
nano /etc/joycmd/joycmd.conf
```
