# Zephyr integration

sendspin-cpp is a Zephyr module. Add it to your application's west manifest,
import its dependency manifest, and enable `CONFIG_SENDSPIN`. Dependencies are
fetched by `west update`; CMake does not download anything.

The port supports Zephyr 4.3 and 4.4 with the full GNU C++ library.
It uses native kernel threads, mutexes and condition variables, BSD sockets,
Zephyr's WebSocket client and `sys_csrand_get()` for Noise encryption and pairing.
It does not require POSIX threads, IXWebSocket, or patches to Zephyr. The monotonic clock uses the
hardware cycle counter for microsecond timestamps, extending 32-bit counters
across wraps and idle periods using kernel uptime.

## Fetch with west

Add this project alongside your existing Zephyr project in `west.yml`:

```yaml
manifest:
  projects:
    # Keep your existing Zephyr project and other dependencies here.
    - name: sendspin-cpp
      url: https://github.com/Sendspin/sendspin-cpp
      revision: main
      path: modules/lib/sendspin-cpp
      import: true
```

For production, pin a reviewed commit or a release that contains the Zephyr
module.

```sh
west update
west build -p always -b YOUR_BOARD modules/lib/sendspin-cpp/examples/zephyr_client
```

The import fetches ArduinoJson v7.4.1, noise-c v0.1.13 and micro-flac v0.3.0.
Zephyr discovers `zephyr/module.yml` automatically. Your app only needs its usual
`find_package(Zephyr ...)` and includes such as `<sendspin/client.h>`; do not call
`add_subdirectory()` on the repository's host-oriented root CMakeLists.txt.

For a checkout outside west, add the repository to `ZEPHYR_EXTRA_MODULES` and set
`SENDSPIN_ARDUINOJSON_DIR`, `SENDSPIN_NOISE_DIR` and, with player/source enabled,
`SENDSPIN_MICRO_FLAC_DIR` to existing dependency checkouts. These CMake path
options also override west's dependency locations.

## Application configuration

A starting point for a networked board with sufficient RAM:

```ini
CONFIG_CPP=y
CONFIG_STD_CPP20=y
CONFIG_NEWLIB_LIBC=y
CONFIG_SENDSPIN=y
CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_TCP=y
CONFIG_ENTROPY_GENERATOR=y
CONFIG_MAIN_STACK_SIZE=8192
CONFIG_HEAP_MEM_POOL_SIZE=65536
CONFIG_NEWLIB_LIBC_MIN_REQUIRED_HEAP_SIZE=262144
```

Enable the board's network and entropy drivers. Add DHCP or static addressing
and DNS as appropriate for the product. `CONFIG_SENDSPIN` requires C++20 or
newer and selects a full C++ standard library, WebSockets and dynamic kernel
stacks. A cryptographic random generator is mandatory. The test project enables
Zephyr's insecure test generator only for simulated boards; do not use it in firmware.

Select roles with `CONFIG_SENDSPIN_ENABLE_PLAYER`, `_CONTROLLER`, `_METADATA`,
`_COLOR`, `_ARTWORK`, `_VISUALIZER` and `_SOURCE`. Player, controller and metadata
are enabled by default. Disabling both player and source removes the micro-flac
build dependency. PCM and FLAC are supported; Opus and Ogg containers are not
included in this port.

`CONFIG_SENDSPIN_THREAD_STACK_SIZE` sets a minimum stack per worker (8192 bytes
by default). Larger per-role stack requests are honored. Dynamic stacks come
from the kernel heap; C++ objects and Noise state use the C heap. Native worker
control blocks, URL conversion storage, JSON and audio buffers use the platform
allocator. Size both heaps and role buffer capacities for the application.
Artwork and audio defaults can exceed MCU RAM: configure them explicitly and
check `start()` failures. By default, allocations use the configured libc heap. An optional
`CONFIG_SENDSPIN_BUFFER_HEAP_SIZE` reserves a separate platform-buffer heap,
placed in PSRAM with `CONFIG_ESP_SPIRAM`. Both placement preferences use this
heap. `CONFIG_SENDSPIN_KERNEL_MALLOC` redirects application-wide C allocations
to the kernel heap; set `CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE=0` when using it.
Thread stacks use the kernel heap by default. For constrained boards,
`CONFIG_SENDSPIN_STATIC_THREAD_STACKS` reserves `CONFIG_SENDSPIN_THREAD_COUNT`
stacks of `CONFIG_SENDSPIN_THREAD_STACK_SIZE` bytes in internal RAM. To place
this pool in ESP PSRAM, explicitly enable `CONFIG_SENDSPIN_THREAD_STACKS_PSRAM`
and measure scheduling and audio performance on the board. The pool never
falls back to the shared heap. Oversized requests or exhaustion refuse worker
creation: client/role startup returns false, and an outbound attempt reports
its transport closed. Per-role PSRAM flags remain ignored. Persistence callbacks
must run on an internal application stack when worker stacks are in PSRAM.

Library diagnostics use Zephyr logging, controlled by `CONFIG_SENDSPIN_LOG_LEVEL`.
The runtime log-level setter has no effect on Zephyr.

`CONFIG_SENDSPIN_THREAD_PRIORITY` sets the base Zephyr priority for Sendspin's
priority 5. Higher Sendspin priorities map to lower Zephyr priority numbers,
clamped to available preemptive priorities. Inspect stack high-water marks and
scheduling on the target board before choosing smaller budgets.

## Connect and integrate audio

[The example](../examples/zephyr_client/src/main.cpp) registers controller and
metadata roles, waits for IPv4 readiness, displays dynamic pairing codes and
calls `connect_to(CONFIG_SENDSPIN_SAMPLE_SERVER_URL)`. Set the URL in your
application configuration, for example:

```ini
CONFIG_SENDSPIN_SAMPLE_SERVER_URL="ws://192.168.1.20:8927/sendspin"
```

The application must call `client.loop()` regularly on the main thread, provide
`SendspinNetworkProvider`, and initiate a new `connect_to()` after a lost
connection according to its reconnect policy. The example initiates one attempt
per network-up interval. It does not retry a failed server while the network
remains up. DNS names require `CONFIG_DNS_RESOLVER`; bracketed IPv6 URLs require
the corresponding network configuration.

The transport supports **outbound `ws://` connections only**. It does not open
the inbound Sendspin listener or advertise an mDNS service, and it does not
support `wss://`. Sendspin's mandatory Noise encryption and pairing remain
active on `ws://`. The HTTP server settings in `SendspinClientConfig` have no
effect on this port. TCP and WebSocket upgrade waits are bounded; DNS lookup
uses Zephyr's resolver timeout. Shutdown may wait for an in-progress lookup or
upgrade to finish.

For playback, enable the player role and implement `PlayerRoleListener` with
the board's audio/I2S driver, including `on_audio_output()` and the `AudioFormat`
supplied with each write. Report completed playback using `PlayerRole::notify_audio_played()` for accurate synchronization. See the
[Integration Guide](integration-guide.md) and [Playback Sync](playback-sync.md)
for the callback contracts. The portable source role similarly needs an
application capture driver. This module does not configure audio hardware.

For persistent identity and pairing, implement `SendspinPersistenceProvider`
using Zephyr settings/NVS or another byte store. Without one, the example's
identity and pairing records are regenerated after reboot.

## Verification

The Zephyr ztest application compiles every role, including PCM/FLAC player and
source code, and exercises native threads, flag timeouts, ring reclamation and
client start/stop/restart and refusal/retry when worker resources are exhausted.
A loopback WebSocket peer checks a masked client send,
large-message reception, ping/pong, interleaved continuation frames and transport
cancellation. The host unit suite checks URL validation with accepted and
rejected addresses in one table:

```sh
west twister -T modules/lib/sendspin-cpp/tests/zephyr \
  -T modules/lib/sendspin-cpp/tests/zephyr_websocket_rx \
  -p qemu_cortex_a9 -p native_sim/native/64 --inline-logs
```

The dedicated WebSocket receive suite supplies interrupted TCP byte streams to
Zephyr's production parser, verifying partial timeouts, masking and empty frames
without wall-clock assertions. Both simulated platforms run in CI; native_sim
also exercises the reserved stack pool.

The transport starts each frame with a one-byte payload read, then tracks the
remaining bytes locally. This preserves framing on Zephyr versions that return
partial payloads on timeout without updating their output metadata; it requires
no changes to Zephyr's WebSocket implementation.

The sample and tests do not establish hardware audio performance or Sendspin
server interoperability; verify those on the deployment board.

Module discovery follows Zephyr's [module documentation](https://docs.zephyrproject.org/4.3.0/develop/modules.html);
dependency imports follow the [west manifest specification](https://docs.zephyrproject.org/4.3.0/develop/west/manifest.html).
