# IBM MQ C++ Client Demo (Docker Compose)

A three-container demonstration of an IBM MQ queue manager communicating with custom-built C++ client applications, orchestrated entirely via Docker Compose. This project includes a **put client** (sends a message to a queue) and a **get client** (retrieves that message), both compiled from source against the IBM MQ Advanced for Developers C++ class libraries.

## System Requirements

**This setup was built and tested on Ubuntu (24.04 LTS).** While Docker itself is cross-platform, the build process, file permission handling, and some of the underlying IBM MQ runtime behaviors documented here were specifically diagnosed and fixed on Ubuntu. If you're running Windows or macOS, expect to encounter additional platform-specific issues not covered in this README (particularly around Docker Desktop's networking/filesystem differences).

You will need:

- Ubuntu 24.04 LTS (or a closely compatible Debian-based distribution)
- Docker Engine
- Docker Compose (v2, i.e., the `docker compose` subcommand, not the standalone legacy `docker-compose`)
- The `10.0.0.0-IBM-MQ-Advanced-for-Developers-UbuntuLinuxX64.tar.gz` installer package (not included in this repo — see [Obtaining the MQ Installer](#obtaining-the-mq-installer) below)
- Roughly 3-4 GB of free disk space for the built images (the runtime images intentionally copy the full IBM MQ installation tree rather than a slimmed-down subset — see [Why the Images Are Large](#why-the-images-are-large))

## What This Demonstrates

- Building a custom C++ application against IBM MQ's C++ class libraries (`ImqQueueManager`, `ImqQueue`, `ImqMessage`) inside a multi-stage Docker build
- Running an official IBM MQ queue manager container alongside custom client containers, all connected via Docker Compose's internal networking
- Client-to-server MQ authentication using MQCSP (username/password) against a `DEV.APP.SVRCONN` channel
- Secure credential handling via Docker Compose secrets (avoiding deprecated plaintext environment variables)
- Connection retry/backoff logic to handle container startup timing gracefully

## Expected Outcome

Running this stack performs a full **put → get round trip**:

1. The `ibmmq` container starts, creates a queue manager named `QM1`, and becomes healthy.
2. The `mq-messageclient` container connects to `QM1` over TCP (via the `DEV.APP.SVRCONN` channel), authenticates, and puts the message `"Hello world from IBM MQ C++ Message Client"` onto the queue `DEV.QUEUE.1`. It then exits with code `0`.
3. The `mq-messagegetter` container waits for the putter to finish, then connects, retrieves that same message from `DEV.QUEUE.1`, prints it to the console, and exits with code `0`.

A successful run's log output will include these lines (among much more verbose IBM MQ startup logging):

```
mq-client  | Connected to Queue Manager: QM1
mq-client  | Successfully sent message to DEV.QUEUE.1
mq-client exited with code 0

mq-getter  | Connected to Queue Manager: QM1
mq-getter  | Received message (255 bytes): Hello world from IBM MQ C++ Message Client
mq-getter exited with code 0
```

You can also log into the IBM MQ web console at `https://localhost:9443/ibmmq/console/` using the `admin` user (see [Secrets Setup](#secrets-setup) for the password) to inspect the queue manager, queues, and channels visually.

## Project Structure

```
.
├── docker-compose.yaml
├── Dockerfile.client
├── Dockerfile.getter
├── mq_messageclient.cpp
├── mq_messagegetter.cpp
├── secrets/
│   ├── mqAppPassword.txt      (you create this - not committed)
│   └── mqAdminPassword.txt    (you create this - not committed)
└── 10.0.0.0-IBM-MQ-Advanced-for-Developers-UbuntuLinuxX64.tar.gz  (you provide this - not committed)
```

## Obtaining the MQ Installer

This repository does **not** include the IBM MQ installer tarball, as it is a large, licensed IBM distribution file. Download `10.0.0.0-IBM-MQ-Advanced-for-Developers-UbuntuLinuxX64.tar.gz` from IBM's official developer downloads, and place it in the root of this project directory (alongside `Dockerfile.client`) before building.

## Secrets Setup

The queue manager container requires two password secrets, mounted via Docker Compose. Create them before your first run:

Sets up the local secret files Compose will mount into the `ibmmq` container at `/run/secrets/`.
```bash
    mkdir -p secrets
    echo -n "<user-created-password>" > secrets/mqAppPassword.txt
    echo -n "<user-created-admin-password>" > secrets/mqAdminPassword.txt
```

**Important**: Do not commit the `secrets/` directory to version control. Add it to `.gitignore`:

```
secrets/
*.tar.gz
```

If you change these passwords (recommended), you must also update the matching `setUserId()`/`setPassword()` calls in `mq_messageclient.cpp` and `mq_messagegetter.cpp` to match, then rebuild.

## How to Run

From the project root, with the MQ installer tarball and `secrets/` directory both in place:

Builds all images from scratch and starts the full stack, streaming logs from all three containers to your terminal.
```bash
    docker compose up --build
```

The first build will take several minutes (expect 2-4 minutes), since it installs the full IBM MQ SDK and compiles both C++ clients. Subsequent builds will be faster due to Docker layer caching, unless you modify the `.cpp` files or Dockerfiles.

Watch the log output for the two success lines described in [Expected Outcome](#expected-outcome) above. Once both `mq-client` and `mq-getter` have exited with code `0`, the demonstration is complete. The `ibmmq` container will continue running afterward — press `Ctrl+C` to stop it, or run in detached mode instead:

```bash
docker compose up --build -d
docker compose logs -f
```

### Clean Restart

If you want to fully reset the queue manager (e.g., after changing passwords or MQSC configuration), do a full teardown before rebuilding, since the queue manager's state persists in the container's writable layer between runs otherwise:

```bash
docker compose down
docker compose up --build
```

### Rebuilding a Single Service

If you only changed one `.cpp` file:

```bash
docker compose build mq-messageclient
docker compose up
```

## Why the Images Are Large

Both client runtime images copy IBM MQ's **entire** `/opt/mqm` installation tree, rather than a hand-picked subset of files. This was a deliberate decision made after extensive troubleshooting: the IBM MQ C++ client runtime depends on a surprisingly scattered set of auxiliary files beyond just its shared libraries — including NLS message catalogs (`/opt/mqm/msg`), CCSID character-set conversion tables (found in *two* separate locations, `/opt/mqm/lib/iconv` and a runtime-expected path under `/var/mqm/conv/table`), licensing metadata, and installation registry files. Attempts to trim the runtime image to only the specific files each binary was observed to open (verified via `strace` against the pre-built IBM sample binaries) proved fragile — different code paths in the client library touched different auxiliary files, and copying an incomplete set produced confusing, generic error codes (`MQRC_ENVIRONMENT_ERROR`, `MQRC_UNEXPECTED_ERROR`) rather than clear "file not found" messages.

Copying the full tree trades additional image size (a few hundred extra MB) for confirmed reliability. If you want to revisit slimming these images down, treat it as an experiment to validate thoroughly (rebuild with `--no-cache`, run a full put/get cycle, and check `/var/mqm/errors/*.FDC` for any First Failure Data Capture reports if something breaks) rather than assuming any particular file list is complete.

## Architecture Notes

- **Service discovery**: Containers reference each other by their Compose service name (e.g., `ibmmq`), which resolves automatically via Compose's internal DNS on the shared network. No manual IP configuration is needed or should be attempted.
- **Connection retry logic**: Both C++ clients retry their connection to the queue manager up to 15 times with a 3-second delay between attempts. This is intentional — the official IBM MQ container's healthcheck (`dspmq -m QM1`) can report "healthy" before the channel listener is actually accepting TCP connections, due to the container's internal create→restart lifecycle. Relying on `depends_on: condition: service_healthy` alone is not sufficient; the retry loop is the actual safety net.
- **Authentication**: The `DEV.APP.SVRCONN` channel requires MQCSP-based username/password authentication by default. This is configured explicitly in code via `setUserId()`, `setPassword()`, and critically, `setAuthenticationType(MQCSP_AUTH_USER_ID_AND_PWD)` — omitting that last call causes credentials to be silently ignored (client-side default is `MQCSP_AUTH_NONE`).

## Troubleshooting

| Symptom | Likely Cause |
|---|---|
| `imqi.hpp: No such file or directory` during build | The MQ SDK `.deb` package wasn't installed correctly in the builder stage — verify `mqlicense.sh -accept` and `apt-get install ./*.deb` both completed without error |
| Reason code `2012` at runtime | Missing NLS message catalog or CCSID table files in the runtime image — check `/var/mqm/errors/*.FDC` inside the container for the specific missing file |
| Reason code `2035` at runtime | Authentication failure — verify the `secrets/mqAppPassword.txt` content matches the `setPassword()` value in the C++ source, and that `setAuthenticationType(MQCSP_AUTH_USER_ID_AND_PWD)` is present |
| Reason code `2538` at runtime | Host/connection name cannot be resolved or reached — confirm the target hostname in `ImqChannel::setConnectionName()` matches the Compose service name exactly |
| `MQ_APP_PASSWORD is deprecated` warning | You're using the legacy environment variable approach instead of Compose secrets — see [Secrets Setup](#secrets-setup) |
