# Git + SSH Raspberry Pi Deploy Workflow

This document describes how to set up the same workflow for any project:

```text
Work on PC -> commit to Git -> push to GitHub -> SSH to Raspberry Pi -> pull -> build -> optionally run
```

Use this when the Raspberry Pi should build and run its own local copy, while the
PC remains the main development machine.

## 1. Required Pieces

On the PC:

- Git
- OpenSSH client
- VS Code, optional but useful
- A GitHub account

On the Raspberry Pi:

- Raspberry Pi OS or another Linux image
- SSH server enabled
- Git
- Project build tools, for example CMake/Ninja/GCC for C++ projects

Example C++ install command on Raspberry Pi:

```bash
sudo apt update
sudo apt install -y git cmake ninja-build build-essential
```

For OpenCV projects, also install:

```bash
sudo apt install -y libopencv-dev
```

## 2. Prepare The Project On The PC

Open a terminal in the project folder.

```powershell
cd C:\path\to\your\project
```

Initialize Git if the project is not already a Git repo:

```powershell
git init
```

Create or update `.gitignore`. Keep generated files, private config, build
folders, and secrets out of Git.

Common C++/CMake example:

```gitignore
build/
build-*/
CMakeFiles/
CMakeCache.txt
cmake_install.cmake
compile_commands.json

*.obj
*.pdb
*.ilk
*.exe
*.dll
*.lib
*.exp
*.res
*.manifest

*.o
*.a
*.so

.vscode/.cmake/
.vscode/c_cpp_properties.json

config.json
.env

Thumbs.db
.DS_Store
```

Optional but recommended: add `.gitattributes` for clean Windows/Linux line
endings.

```gitattributes
* text=auto

*.sh text eol=lf
*.ps1 text eol=crlf
*.bat text eol=crlf
```

Make the first commit:

```powershell
git add .
git commit -m "Initial project commit"
git branch -M main
```

## 3. Create An Empty GitHub Repository

In GitHub:

1. Open `https://github.com/new`.
2. Choose owner.
3. Enter repository name.
4. Choose Public or Private.
5. Do not add README.
6. Do not add `.gitignore`.
7. Do not add a license.
8. Create repository.

Copy the SSH URL. It looks like:

```text
git@github.com:USERNAME/REPO.git
```

Connect the local project to GitHub:

```powershell
git remote add origin git@github.com:USERNAME/REPO.git
git push -u origin main
```

If the push fails with `Permission denied (publickey)`, set up a PC SSH key as
shown in the next section.

## 4. Set Up PC SSH Key For GitHub

On the PC, create an SSH key:

```powershell
ssh-keygen -t ed25519 -C "your-email@example.com" -f "$env:USERPROFILE\.ssh\id_ed25519"
```

When it asks for a passphrase, press Enter twice if you want no passphrase for
one-button deploy commands.

Print the public key:

```powershell
Get-Content $env:USERPROFILE\.ssh\id_ed25519.pub
```

Copy the full line starting with `ssh-ed25519`.

In GitHub:

1. Open `https://github.com/settings/keys`.
2. Click `New SSH key`.
3. Title: something like `Main Windows PC`.
4. Key type: `Authentication Key`.
5. Paste the public key.
6. Save.

Test from the PC:

```powershell
ssh -T git@github.com
```

Expected result:

```text
Hi USERNAME! You've successfully authenticated, but GitHub does not provide shell access.
```

Now push again:

```powershell
git push -u origin main
```

## 5. Set Up Raspberry Pi SSH Key For GitHub

Open a terminal on the Raspberry Pi, either directly or through PuTTY/SSH:

```bash
ssh pi@raspberrypi.local
```

or:

```bash
ssh pi@PI_IP_ADDRESS
```

Create an SSH key on the Pi:

```bash
mkdir -p ~/.ssh
chmod 700 ~/.ssh
ssh-keygen -t ed25519 -C "raspberry-pi-project-name" -f ~/.ssh/id_ed25519
```

When it asks for a passphrase, press Enter twice if you want unattended pulls.

Print the Pi public key:

```bash
cat ~/.ssh/id_ed25519.pub
```

Copy the full line starting with `ssh-ed25519`.

Recommended GitHub option for a Pi that only pulls this one repository:

1. Open the repository on GitHub.
2. Go to `Settings`.
3. Go to `Deploy keys`.
4. Click `Add deploy key`.
5. Title: `Raspberry Pi`.
6. Paste the public key.
7. Leave `Allow write access` unchecked.
8. Save.

Alternative GitHub option:

1. Open `https://github.com/settings/keys`.
2. Click `New SSH key`.
3. Key type: `Authentication Key`.
4. Paste the Pi public key.
5. Save.

Test from the Pi:

```bash
ssh -T git@github.com
```

If Git asks whether to trust `github.com`, type `yes` after verifying the host
fingerprint from GitHub's official documentation.

## 6. Clone The Project On The Raspberry Pi

On the Pi:

```bash
git clone git@github.com:USERNAME/REPO.git ~/REPO
cd ~/REPO
git status
```

Expected status:

```text
On branch main
Your branch is up to date with 'origin/main'.
nothing to commit, working tree clean
```

Create any Pi-local config file that should not be committed.

Example:

```bash
cp config.example.json config.json
nano config.json
```

## 7. Build Manually On The Raspberry Pi Once

For a CMake/Ninja C++ project:

```bash
cd ~/REPO
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Run the app if appropriate:

```bash
./build/YOUR_TARGET
```

Do this manual build once before automating deployment. It separates dependency
problems from deploy-script problems.

## 8. Set Up Passwordless PC-To-Pi SSH

The PC must be able to SSH into the Pi without typing a password.

On the PC, print the PC public key:

```powershell
Get-Content $env:USERPROFILE\.ssh\id_ed25519.pub
```

Copy the full line.

On the Pi:

```bash
mkdir -p ~/.ssh
chmod 700 ~/.ssh
nano ~/.ssh/authorized_keys
```

Paste the PC public key on its own line. Save and exit.

Then:

```bash
chmod 600 ~/.ssh/authorized_keys
```

From the PC, test:

```powershell
ssh pi@PI_IP_ADDRESS "echo pi-ssh-ok"
```

Expected:

```text
pi-ssh-ok
```

## 9. Add A PC Deploy Script

Create `scripts/deploy-pi.ps1` in the project.

Replace:

- `pi@PI_IP_ADDRESS`
- `/home/pi/REPO`
- `YOUR_TARGET`

```powershell
[CmdletBinding()]
param(
    [string]$PiHost = "pi@PI_IP_ADDRESS",
    [string]$ProjectDir = "/home/pi/REPO",
    [string]$BuildDir = "build",
    [string]$Generator = "Ninja",
    [string]$Target = "YOUR_TARGET",
    [switch]$Run,
    [switch]$SkipPush
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
    param(
        [string]$Title,
        [scriptblock]$Command
    )

    Write-Host ""
    Write-Host "==> $Title"
    & $Command
}

Invoke-Step "Checking local Git state" {
    $branch = (git branch --show-current).Trim()
    if (-not $branch) {
        throw "Could not determine the current Git branch."
    }

    $dirty = git status --porcelain
    if ($dirty) {
        throw "Working tree has uncommitted changes. Commit them before deploying to the Raspberry Pi."
    }

    Write-Host "Branch: $branch"
    Set-Variable -Name CurrentBranch -Value $branch -Scope Script
}

if (-not $SkipPush) {
    Invoke-Step "Pushing local branch to GitHub" {
        git push origin $script:CurrentBranch
    }
}

$remoteCommands = @(
    "set -e",
    "cd '$ProjectDir'",
    "git pull --ff-only",
    "cmake -S . -B '$BuildDir' -G '$Generator' -DCMAKE_BUILD_TYPE=Release",
    "cmake --build '$BuildDir' -j`$(nproc)"
)

if ($Run) {
    $remoteCommands += "./$BuildDir/$Target"
}

$remoteCommand = $remoteCommands -join " && "

Invoke-Step "Deploying on Raspberry Pi $PiHost" {
    ssh $PiHost $remoteCommand
}

Write-Host ""
Write-Host "Deploy complete."
```

Commit and push the deploy script:

```powershell
git add scripts/deploy-pi.ps1
git commit -m "Add Raspberry Pi deploy script"
git push
```

Run from the PC:

```powershell
.\scripts\deploy-pi.ps1
```

Run and start the app:

```powershell
.\scripts\deploy-pi.ps1 -Run
```

## 10. Add VS Code One-Button Tasks

Create or update `.vscode/tasks.json`:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "type": "shell",
      "label": "Deploy: Raspberry Pi build",
      "command": "powershell",
      "args": [
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        "${workspaceFolder}/scripts/deploy-pi.ps1"
      ],
      "options": {
        "cwd": "${workspaceFolder}"
      },
      "problemMatcher": [],
      "detail": "Push current branch, pull it on Raspberry Pi, configure, and build."
    },
    {
      "type": "shell",
      "label": "Deploy: Raspberry Pi build and run",
      "command": "powershell",
      "args": [
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        "${workspaceFolder}/scripts/deploy-pi.ps1",
        "-Run"
      ],
      "options": {
        "cwd": "${workspaceFolder}"
      },
      "problemMatcher": [],
      "detail": "Push current branch, pull it on Raspberry Pi, configure, build, and run."
    }
  ]
}
```

Use it in VS Code:

```text
Terminal -> Run Task... -> Deploy: Raspberry Pi build
```

## 11. Daily Workflow

On the PC:

```powershell
git status
git add .
git commit -m "Describe the change"
```

Then either run:

```powershell
.\scripts\deploy-pi.ps1
```

or use VS Code:

```text
Terminal -> Run Task... -> Deploy: Raspberry Pi build
```

The Pi receives the update with:

```bash
git pull --ff-only
```

and builds locally.

## 12. Troubleshooting

`Permission denied (publickey)` from GitHub:

- The machine running Git does not have an SSH key added to GitHub.
- Check with `ssh -T git@github.com`.
- Make sure the remote uses SSH, not HTTPS:

```powershell
git remote -v
git remote set-url origin git@github.com:USERNAME/REPO.git
```

`Permission denied` when PC SSHs to Pi:

- The PC public key is not in `/home/pi/.ssh/authorized_keys`.
- Check permissions:

```bash
chmod 700 ~/.ssh
chmod 600 ~/.ssh/authorized_keys
```

`Host key verification failed`:

- The PC has not trusted the Pi host key yet, or the Pi was reinstalled and got
  a new host key.
- For first connection, run:

```powershell
ssh pi@PI_IP_ADDRESS
```

and confirm the host if it is your Pi.

`git pull --ff-only` fails:

- The Pi has local changes.
- Inspect on the Pi:

```bash
cd ~/REPO
git status
```

Do not overwrite local changes unless you know they are disposable.

CMake cannot find a dependency:

- Install the dependency on the Pi.
- For OpenCV:

```bash
sudo apt install -y libopencv-dev
```

The app builds but cannot display a window:

- GUI apps need a display session.
- PuTTY alone is usually only a terminal.
- Run from the Pi desktop, configure X forwarding, or add a headless mode to
  the app.

## 13. Security Notes

- Never commit private keys.
- Never commit passwords, tokens, `.env`, or machine-local `config.json`.
- A deploy key without write access is safest for a Pi that only pulls one repo.
- An account Authentication Key is simpler but grants access according to the
  GitHub account permissions.
- Keep the Pi reachable only on trusted networks unless you intentionally harden
  it for internet exposure.
