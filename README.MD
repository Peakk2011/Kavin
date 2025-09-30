# Kavin

The generic Node.js process watcher written in C and Assembly

## Quick Install
```bash
git clone https://github.com/Peakk2011/Kavin.git
cd Kavin
make
```

It's a better alternative for viewing files that works like nodemon and has some nice differences.

## Why Kavin over nodemon?

| Feature | Kavin | nodemon |
|---------|-------|---------|
| **Startup Time** | <10ms | ~200ms |
| **Memory Usage** | ~2MB | ~30MB |
| **CPU Usage** | ~0.1% | ~1-2% |
| **Binary Size** | ~18KB | ~1.5MB (+ Node.js) |
| **Dependencies** | None | Node.js + npm packages |
| **Language** | C/ASM | JavaScript |

### Key Advantages

- **15-20x faster startup** - Made with the low-level programming language, the execution time can be done immediately.
- **15x less memory** - No Node.js runtime overhead  
- **10-20x less CPU** - Minimal resource consumption  
- **Zero dependencies** - Single executable, works anywhere  
- **Universal** - Works with Node.js, Deno, Bun, Python, Go, Rust, anything  
- **Smart process management** - Kills entire process tree cleanly (no zombies)  
- **Graceful shutdown** - SIGTERM → wait → SIGKILL if needed  
- **Crash recovery** - Auto-restarts if process dies unexpectedly  

### When to use Kavin

**Choose Kavin if you want:**
- Maximum speed and minimum resource usage
- Lightweight tool that does one thing extremely well
- No Node.js dependency (works with any language/runtime)
- Simple single-file watching
    - Like you want to live some file and fetch some command it will auto to run
    - Example: `./kavin "npm start" src/main.js`

**Choose nodemon if you need:**
- Complex directory watching with patterns
- Advanced configuration via nodemon.json
- Plugin ecosystem and extensive options

**Think of it this way:**

Thinking this way will give you a bigger picture, not just technical information.

`nodemon` = Swiss Army knife (feature-rich, versatile)  
`Kavin` = Scalpel (sharp, fast, precise)

## Installation

### Build from source

```bash
make
```

This creates the `kavin` executable in your current directory.

### Install globally (optional)

```bash
make install
```

## Usage

### Basic usage

```bash
./kavin "npm start" src/main.js
./kavin "node server.js" server.js
./kavin "bun run dev" index.ts
```

### Command format

```bash
./kavin "<command>" [file_to_watch]
```

- `<command>` - Any shell command to run
- `[file_to_watch]` - File to watch (default: `src/main.js`)

## How it works

```
┌─────────────────────────────────────────┐
│  1. Start: ./kavin "npm start" main.js  │
└─────────────────┬───────────────────────┘
                  │
                  ▼
         ┌────────────────┐
         │ Fork & Execute │
         │  "npm start"   │
         │   (PID: 1234)  │
         └────────┬───────┘
                  │
        ┌─────────▼──────────┐
        │   Watch main.js    │
        │  Check every 0.1s  │
        └─────────┬──────────┘
                  │
          ┌───────┴────────┐
          │                │
    File changed?    Process died?
          │                │
         Yes              Yes
          │                │
          ▼                ▼
    ┌──────────────────────────┐
    │  1. SIGTERM → wait 2s    │
    │  2. SIGKILL if alive     │
    │  3. Fork new process     │
    └──────────────────────────┘
                  │
                  ▼
          ┌───────────────┐
          │ Restart count │
          │     +1        │
          └───────────────┘
```

### What happens on file change:

1. **Detect change** - `stat()` checks file modification time
2. **Kill gracefully** - Send `SIGTERM` to process group
3. **Wait patiently** - Give 2 seconds to clean up
4. **Force kill** - Send `SIGKILL` if still alive
5. **Restart** - Fork new process immediately
6. **Track** - Increment restart counter

### Process Management

**Process Group Isolation:**
```c
setpgid(0, 0);          // Child creates new process group
kill(-pid, SIGTERM);    // Kill entire group
```

This ensures:
- All child processes are killed (npm → node → your app)
- No zombie processes left behind
- Clean process tree termination

## Examples

### Electron apps

```bash
./kavin "npm start" src/main.js
./kavin "electron ." src/main.js
./kavin "electron-forge start" src/index.js
```

### Node.js servers

```bash
./kavin "node server.js" server.js
./kavin "npm run dev" src/index.js
./kavin "ts-node src/app.ts" src/app.ts
```

### Bun / Deno

```bash
./kavin "bun run dev" src/app.ts
./kavin "deno run --allow-net server.ts" server.ts
```

### Other languages

```bash
./kavin "python app.py" app.py
./kavin "go run main.go" main.go
./kavin "cargo run" src/main.rs
```

## Real-world Benefits

### Development Workflow

**Before Kavin:**
```bash
$ npm start
# ... edit code ...
^C
$ npm start
# ... edit code ...
^C
$ npm start
# Repeat 100x per day 😫
```

> There may be other tools that don't always require this. This example is a general explanation.

**With Kavin:**
```bash
$ ./kavin "npm start" src/main.js
# ... edit code, save ...
# Auto-restarts!
# ... edit code, save ...
# Auto-restarts!
# Keep coding!
```

### Performance Impact

On a typical development session (8 hours):
- **100 manual restarts** → 100 × 5 seconds = **8 minutes wasted**
- **With nodemon** → 30MB RAM + 1-2% CPU constantly
- **With Kavin** → 2MB RAM + 0.1% CPU constantly

**Result:** Kavin saves time, battery, and system resources.

## Configuration

Edit `watcher/watcher.h` to change defaults:

```c
#define FILEPATH "src/main.js"         // Default file to watch
#define CHECK_INTERVAL_US 100000       // Check interval (0.1s)
```

Then rebuild:
```bash
make rebuild
```

## Project Structure

> This structure is a basic representation of the project structure. There may be additional structures and folders.

```
kavin/
├── main.c                      # Entry point, argument parsing
├── watcher/                   
│   ├── watcher.c/.h            # File watching logic (stat, mtime)
│   ├── watcher_actions.c/.h    # Restart, kill process tree
│   └── process.c/.h            # Process management (fork, exec)
├── Makefile                    # Build automation
├── README.md
└── LICENSE
```

## Development

### Build commands

```bash
make           # Build optimized binary
make clean     # Remove build artifacts
make rebuild   # Clean + build
make install   # Install to /usr/local/bin
```

## Troubleshooting

### Process not killing properly

Kavin uses process groups to kill entire trees. If you still see zombie processes:

```bash
# Check for orphaned processes
ps aux | grep node

# Kill manually if needed
kill -9 <pid>
```

### File changes not detected

Make sure the file path is correct:

```bash
# Wrong (if run from project root)
./kavin "npm start" main.js

# Correct
./kavin "npm start" src/main.js
```

### Permission denied

Make the binary executable:

```bash
chmod +x kavin
```

### High CPU usage

If you see high CPU, your editor might be creating temp files. Adjust check interval:

```c
#define CHECK_INTERVAL_US 200000  // Check every 0.2s instead
```

## License

MIT License - see [LICENSE](LICENSE) file for details

## Contributing

Pull requests welcome! Please keep the philosophy of simplicity and speed.

## Author

Made by Mint teams

---

**Star ⭐ this repo if Kavin saves you time!**
