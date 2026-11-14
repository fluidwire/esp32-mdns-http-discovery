# esp32-mdns-http-discovery — reach an ESP32 web server at a friendly .local hostname with mDNS service advertising

IoT example by [Fluidwire](https://fluidwire.com/).

An ESP32 sketch that runs a small web server and claims a name on the local network with mDNS, so you open `http://fluidwire-node.local/` instead of hunting for whatever IP the router handed out this time. The board also advertises its server as an `_http._tcp` service with TXT records, and exposes a `/api/browse` endpoint that lists every other `_http._tcp` device it can see — the piece most thesis prototypes with several nodes end up needing once "just check the router's DHCP table" stops scaling. After a WiFi drop the sketch re-registers mDNS automatically so the name keeps resolving.

## Required hardware

- 1x ESP32 dev board
- A laptop or phone on the same WiFi network

No external components — this one is all network.

## Required libraries

- None from Library Manager. `WiFi`, `WebServer` and `ESPmDNS` are built into the ESP32 Arduino core (2.x and 3.x both work — the sketch picks the right `ESPmDNS` API with a version check).

## Setup

1. Copy `config.h.example` to `config.h`. Fill in WiFi, and pick `MDNS_HOSTNAME` (lowercase, digits, hyphens; unique on your network) and a friendly `MDNS_INSTANCE` name.
2. Flash and open Serial Monitor at **115200 baud**.
3. From a device on the same WiFi, open `http://fluidwire-node.local/` (or whatever hostname you chose). The status page refreshes every 10 seconds.
4. Try the JSON endpoints:

```bash
curl http://fluidwire-node.local/api/status
curl http://fluidwire-node.local/api/browse     # takes ~3 s, lists other _http._tcp devices
```

5. Browse for the service from your laptop to see the TXT records:

```bash
# Linux (avahi-utils)
avahi-browse -rt _http._tcp
# macOS
dns-sd -B _http._tcp
# Windows: Bonjour Browser, or the "Service Browser" app on Android
```

## Endpoints

| Path | Returns |
|------|---------|
| `/` | HTML status page: hostname, IP, RSSI, uptime, free heap, request count |
| `/api/status` | Same data as JSON |
| `/api/browse` | JSON list of `_http._tcp` services on the LAN (hostname, ip, port, txt) |

## How mDNS fits together

- **Hostname** — `MDNS.begin("fluidwire-node")` answers queries for `fluidwire-node.local` with the board's IP. `WiFi.setHostname()` is also called so the router's DHCP table shows the same name.
- **Service** — `MDNS.addService("http", "tcp", 80)` publishes a `_http._tcp` record; service browsers show it as *Fluidwire ESP32 demo node*. The TXT records (`vendor`, `board`, `path`) are free-form key/value hints a client can read without connecting.
- **Browsing** — `MDNS.queryService("http", "tcp")` sends a PTR query and collects answers for about 3 seconds. Flash this sketch on two boards with different hostnames and each will list the other.
- **Reconnects** — mDNS state belongs to the network interface. When WiFi comes back the sketch calls `MDNS.end()` then `MDNS.begin()` again; without that the name silently stops resolving after the first dropout.

## Expected serial output

```
[main] esp32-mdns-http-discovery
[wifi] connecting to your-wifi-ssid
.....
[http] server listening on port 80
[wifi] IP: 192.168.1.50  hostname: fluidwire-node  RSSI: -58 dBm
[mdns] advertising http://fluidwire-node.local/ (_http._tcp on port 80)
[http] GET / from 192.168.1.20
[http] GET /api/browse from 192.168.1.20
[mdns] browse _http._tcp: 2 found
[status] uptime=30s ip=192.168.1.50 rssi=-58dBm requests=2 mdns=up heap=241536
```

## Troubleshooting

- **`.local` does not resolve from Windows:** older Windows builds need Bonjour (installed with iTunes or the standalone Bonjour Print Services). Windows 10 1709+ and Windows 11 resolve mDNS natively. Try the IP from the serial log to confirm the server itself is fine.
- **Resolves from a laptop but not from a phone:** many phone browsers do not resolve `.local`. Use a service-browser app, or the IP.
- **Works on one WiFi, not another:** campus, office and guest networks often block multicast (224.0.0.251) between clients, which kills mDNS. Test on a home router or a phone hotspot.
- **`[mdns] start FAILED`:** the hostname has illegal characters (dots, spaces, uppercase) or another device already owns it. Change `MDNS_HOSTNAME`.
- **`/api/browse` finds nothing, even itself:** that is expected — a responder does not list its own services. Add a second board, a printer, or run `python3 -m http.server` with Avahi on a Linux laptop to have something to find.
- **Page is slow while someone hits `/api/browse`:** `queryService()` blocks the whole sketch for ~3 s. For production, move browsing to a timer or a FreeRTOS task (see the Fluidwire `esp32-freertos-dual-core-sensor-tasks` sample).
- **Name stops resolving after a router reboot:** check the serial log for `[wifi] reconnected` followed by `[mdns] advertising`; if the first appears without the second, `MDNS.begin()` failed and the error above applies.

## Security note

Never commit `config.h`; it is git-ignored. mDNS advertises the hostname and TXT records to everyone on the LAN, so do not put secrets in TXT values. The web server here has no authentication — fine on a trusted home network, not fine on a shared campus network. See the Fluidwire `esp32-http-basic-auth-server` sample for adding a login.

## License

MIT — see [LICENSE](LICENSE).

## Got stuck?

Drop by [fluidwire.com](https://fluidwire.com/) — happy to help.
