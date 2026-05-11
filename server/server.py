import logging
import time
from datetime import datetime, timezone, timedelta
from flask import Flask, request, jsonify
from datetime import datetime, timezone
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS
import requests

app = Flask(__name__)
logging.basicConfig(level=logging.INFO, format='%(asctime)s %(levelname)s %(message)s')

# ── 1. LOCAL INFLUXDB CONFIG ─────────────────────────────────────────
LOCAL_URL = "http://localhost:8086"
LOCAL_TOKEN = "your token here"
LOCAL_ORG = "lab"
LOCAL_BUCKET = "sensors"

local_client = InfluxDBClient(url=LOCAL_URL, token=LOCAL_TOKEN, org=LOCAL_ORG)
local_write_api = local_client.write_api(write_options=SYNCHRONOUS)

# ── 2. GRAFANA CLOUD CONFIG ──────────────────────────────────────────
CLOUD_URL = "https://prometheus-prod-55-prod-gb-south-1.grafana.net/api/v1/push/influx/write"
CLOUD_USER = "your user id"
CLOUD_TOKEN = "your token here"


@app.route('/time', methods=['GET'])
def get_time():
    now = time.time()
    # Calculates the difference between local time and UTC in seconds
    # During BST this is 3600; during GMT this is 0
    offset = time.localtime(now).tm_gmtoff 
    return f"{int(now)},{int(offset)}"


@app.route('/sensor-data', methods=['POST'])
def receive_data():
    try:
        data = request.get_json()
        if not data:
            return jsonify({"status": "error", "message": "No JSON received"}), 400

        sensor_id = data.get('sensor_id', 'unknown')
        location = data.get('location', 'unknown')
        co2 = data.get('co2')

        if co2 is None:
            return jsonify({"status": "error", "message": "Missing co2 field"}), 400

        # =========================================================
        # TASK A: Write to Local Backup (InfluxDB)
        # =========================================================
        point = (
            Point("gas_reading")
            .tag("sensor_id", sensor_id)
            .tag("location", location)
            .field("co2_ppm", int(co2))
        )
        app.logger.info(f"Incoming timestamp: {data.get('timestamp')}")
        if data.get('timestamp') and int(data['timestamp']) > 1700000000:
            ts = datetime.fromtimestamp(int(data['timestamp']), tz=timezone.utc)
            point = point.time(ts)

        if data.get('o2') is not None:
            point = point.field("o2_pct", float(data['o2']))
        if data.get('uptime') is not None:
            point = point.field("uptime_s", int(data['uptime']))

        try:
            app.logger.info(f"Grafana local payload: {point}")
            local_write_api.write(bucket=LOCAL_BUCKET, org=LOCAL_ORG, record=point)
        except Exception as e:
            app.logger.error(f"Local write failed: {e}")

        # =========================================================
        # TASK B: Write to Grafana Cloud
        # =========================================================
        # 1. Sanitize tags: Replace spaces and commas with underscores
        safe_sensor_id = str(sensor_id).replace(' ', '_').replace(',', '_')
        safe_location = str(location).replace(' ', '_').replace(',', '_')

        tags = f"sensor_id={safe_sensor_id},location={safe_location}"

        # 2. Format fields: Cast values to float to prevent type-conflict errors
        fields = f"co2_ppm={float(co2)}"

        if data.get('o2') is not None:
            fields += f",o2_pct={float(data['o2'])}"
        if data.get('uptime') is not None:
            fields += f",uptime_s={float(data['uptime'])}"

        # 3. Build string: Must have a space before fields, and a \n at the end
        if data.get('timestamp') and int(data['timestamp']) > 1700000000:
            ts_ns = int(data['timestamp']) * 1_000_000_000  # seconds → nanoseconds
            line_protocol = f"gas_reading,{tags} {fields} {ts_ns}\n"
        else:
            line_protocol = f"gas_reading,{tags} {fields}\n"
        try:
            app.logger.info(f"Grafana payload: {line_protocol.strip()}")
            response = requests.post(
                CLOUD_URL,
                data=line_protocol,
                auth=(CLOUD_USER, CLOUD_TOKEN),
                headers={'Content-Type': 'text/plain'},  # Explicitly tell Grafana this is plain text
                timeout=5
            )

            # Log the specific error text if it fails again so we can see exactly why
            if response.status_code not in [200, 204]:
                app.logger.warning(f"Cloud push failed ({response.status_code}): {response.text}")
        except Exception as e:
            app.logger.warning(f"Cloud connection error: {e}")

        app.logger.info(f"[{sensor_id}] Processed data (Local + Cloud)")
        return jsonify({"status": "success"}), 200

    except Exception as e:
        app.logger.error(f"General Error: {e}")
        return jsonify({"status": "error", "message": str(e)}), 500


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)
