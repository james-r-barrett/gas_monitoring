# Raspberry Pi Server

The Raspberry Pi acts as the central hub for the sensor network. It:

1. Broadcasts a WiFi access point (`GasMonitor`) that the sensors connect to
2. Provides time synchronisation to sensors on connection
3. Receives sensor readings via HTTP POST
4. Stores readings in a local InfluxDB instance
5. Forwards readings to Grafana Cloud

---

## Hardware

- Raspberry Pi 5 (2GB RAM tested; 4GB recommended for comfort)
- microSD card for Pi OS (16GB minimum)
- Ethernet cable (for network connection)
- Power supply (official Pi 5 USB-C 27W recommended)

---

## OS Setup

Flash **Raspberry Pi OS Lite (64-bit)** using [Raspberry Pi Imager](https://www.raspberrypi.com/software/).

In the Imager settings before flashing:

- Hostname: `gasmonitor.local`
- Enable SSH
- Set username and password

Boot the Pi and SSH in:
```bash
ssh pi@gasmonitor.local
```

Update:
```bash
sudo apt update && sudo apt upgrade -y
```

---

## WiFi Access Point Setup

The Pi broadcasts `GasMonitor` on its wireless interface while using ethernet for university network connectivity.

> ⚠️ **For most university networks it is not permitted to operate the Pi as an access point to the university. IPv4 forwarding must be disabled.**

### Disable IPv4 forwarding
Disable immediately:
```bash
sudo sysctl -w net.ipv4.ip_forward=0
```
Make persistent:
```bash
sudo nano /etc/sysctl.conf
```
Add:
```
net.ipv4.ip_forward=0
```
Apply:
```
sudo sysctl -p
```

### Install packages
```bash
sudo apt install -y hostapd dnsmasq
sudo systemctl stop hostapd dnsmasq
```

### Static IP for wlan0
```bash
sudo nano /etc/dhcpcd.conf
```
Add at the bottom:
```
interface wlan0
    static ip_address=192.168.50.1/24
    nohook wpa_supplicant
```

### DHCP server (dnsmasq)
```bash
sudo mv /etc/dnsmasq.conf /etc/dnsmasq.conf.orig
sudo nano /etc/dnsmasq.conf
```
```
interface=wlan0
dhcp-range=192.168.50.10,192.168.50.50,255.255.255.0,24h
domain=local
address=/gasmonitor.local/192.168.50.1
```

### Access point (hostapd)
```bash
sudo nano /etc/hostapd/hostapd.conf
```
```
interface=wlan0
driver=nl80211
ssid=GasMonitor
hw_mode=g
channel=7
wpa=2
wpa_passphrase=your set password here
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
```

```bash
sudo nano /etc/default/hostapd
# Change: DAEMON_CONF="" → DAEMON_CONF="/etc/hostapd/hostapd.conf"
```

### Enable and reboot
```bash
sudo systemctl unmask hostapd
sudo systemctl enable hostapd dnsmasq
sudo reboot
```

After reboot, `GasMonitor` WiFi should be visible. Connect your laptop to it and verify SSH still works at `192.168.50.1`.

---

## InfluxDB

```bash
wget -q https://repos.influxdata.com/influxdata-archive_compat.key
echo '393e8779c89ac8d958f81f942f9ad7fb82a25e133faddaf92e15b16e6ac9ce4c influxdata-archive_compat.key' | sha256sum -c
cat influxdata-archive_compat.key | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/influxdata-archive_compat.gpg > /dev/null
echo 'deb [signed-by=/etc/apt/trusted.gpg.d/influxdata-archive_compat.gpg] https://repos.influxdata.com/debian stable main' | sudo tee /etc/apt/sources.list.d/influxdata.list
sudo apt update && sudo apt install -y influxdb2
sudo systemctl enable --now influxdb
```

Open `http://192.168.50.1:8086` in a browser connected to `GasMonitor` WiFi and complete the setup wizard:

- Organisation: `lab`
- Bucket: `sensors`
- Generate an **All Access API token** and save it — you'll need it below

---

## Flask Receiver

### Install dependencies
```bash
sudo apt install -y python3-flask
pip3 install influxdb-client requests --break-system-packages
```

### Install server script
```bash
mkdir ~/gas_project
cp server.py ~/gas_project/
nano ~/gas_project/server.py
# Edit: paste your InfluxDB token into LOCAL_TOKEN
# Edit: paste your Grafana Cloud credentials into CLOUD_USER and CLOUD_TOKEN
```

### Test
```bash
python3 ~/gas_project/server.py
```

From another terminal or device on the `GasMonitor` network:
```bash
curl -X POST http://192.168.50.1:5000/sensor-data \
  -H "Content-Type: application/json" \
  -d '{"sensor_id":"test","location":"test","co2":500}'
```

You should see `{"status":"success"}`.

### Run as a system service
```bash
sudo nano /etc/systemd/system/gasreceiver.service
```
```ini
[Unit]
Description=Gas Sensor Data Receiver
After=network.target influxdb.service
Requires=influxdb.service

[Service]
ExecStart=/usr/bin/python3 /home/pi/gas_project/server.py
WorkingDirectory=/home/pi/gas_project
Restart=always
RestartSec=5
User=pi

[Install]
WantedBy=multi-user.target
```
```bash
sudo systemctl enable --now gasreceiver
sudo journalctl -u gasreceiver -f   # watch logs
```

---

## Grafana

```bash
sudo mkdir -p /etc/apt/keyrings/
wget -q -O - https://apt.grafana.com/gpg.key | gpg --dearmor | sudo tee /etc/apt/keyrings/grafana.gpg > /dev/null
echo "deb [signed-by=/etc/apt/keyrings/grafana.gpg] https://apt.grafana.com stable main" | sudo tee /etc/apt/sources.list.d/grafana.list
sudo apt update && sudo apt install -y grafana
sudo systemctl enable --now grafana-server
```

Access local Grafana at `http://192.168.50.1:3000` (default login: `admin` / `admin`).

Add InfluxDB as a data source:

- Type: **InfluxDB**
- Query language: **Flux**
- URL: `http://localhost:8086`
- Organisation: `lab`
- Token: paste your InfluxDB token
- Default bucket: `sensors`

---

## Grafana Cloud (optional)

For remote access without a VPN, the server script forwards readings to Grafana Cloud using the InfluxDB line protocol over HTTPS. Configure `CLOUD_URL`, `CLOUD_USER`, and `CLOUD_TOKEN` in `server.py` with credentials from your Grafana Cloud stack.

---

## API Endpoints

The receiver exposes the following HTTP endpoints:

| Endpoint | Method | Description |
|---|---|---|
| `/sensor-data` | POST | Receive a reading from a sensor |
| `/time` | GET | Return current Unix timestamp (used by sensors for time sync) |
| `/health` | GET | Health check |
| `/sensors` | GET | List last reading from each sensor |

### POST /sensor-data payload

```json
{
  "sensor_id": "chamber1",
  "location": "Growth Room A",
  "co2": 850,
  "o2": 20.91,
  "uptime": 3600,
  "timestamp": 1746540130
}
```

Fields `o2`, `uptime`, and `timestamp` are optional. If `timestamp` is provided and valid (>1700000000), it is used as the InfluxDB write time; otherwise the Pi's system time is used.

---

## Troubleshooting

| Problem | Action                                                                  |
|---|-------------------------------------------------------------------------|
| `GasMonitor` WiFi not appearing | `sudo systemctl status hostapd` — check for errors                      |
| Sensors not connecting to Pi | Verify sensor WiFi credentials match `GasMonitor` / `your set password` |
| No data in InfluxDB | Check `sudo journalctl -u gasreceiver -f` for POST errors               |
| Timestamps wrong in Grafana | Run `timedatectl` on Pi — NTP must be active and synced                 |
| Pi can't reach Grafana Cloud | Check ethernet connection: `curl https://grafana.net`                   |
