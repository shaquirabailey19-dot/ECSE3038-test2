# ECSE3038 Test 2 — The bathroom fan

An ESP32 board (Wokwi) that reads humidity and temperature and posts them to a FastAPI backend, which stores fans/readings in MongoDB and pushes fan settings to the board over MQTT.

## Running the API
cd api
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
# create .env with MONGODB_URI and MQTT_NAME
python -m uvicorn app:app --reload

## Running the board
Open `board` in PlatformIO, build and run in Wokwi.
Create `board/include/secrets.h` with `#define DEVICE_KEY ""`, flash once to get your MAC from Serial Monitor, register it via `POST /fans`, paste the returned key into `secrets.h`, rebuild.