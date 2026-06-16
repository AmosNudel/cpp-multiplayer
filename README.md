# Multiplayer Raylib Game

Authoritative multiplayer template for a raylib game.

- **Desktop clients** connect over TCP
- **Web clients** connect over WebSocket
- **Headless server** runs in Docker on Railway

## Quick start

```powershell
.\tools\build.ps1
.\build\game_server.exe          # terminal 1
.\build\game_client.exe          # terminal 2
```

## Documentation

Read **[NETWORKING_GUIDE.md](NETWORKING_GUIDE.md)** for:

- Architecture diagrams
- What each package does and why
- Build targets (desktop, web, installer, Docker)
- How to use networking while developing your game
- Railway deployment steps

## Project structure

| Folder | Purpose |
|---|---|
| `common/` | Shared protocol and socket helpers |
| `server/` | Headless authoritative host |
| `client/` | raylib game + network client |
| `tools/` | Build and installer scripts |
| `config/` | Production server endpoints (`production.env.ps1`, gitignored) |

## Production config (before first git push)

```powershell
copy config\production.env.ps1.example config\production.env.ps1
# Edit production.env.ps1 with your Railway TCP proxy + domain
```

`production.env.ps1` is gitignored. Commit only the `.example` template.
