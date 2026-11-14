import os
import secrets
from typing import Literal
from datetime import datetime, timezone

from fastapi import FastAPI, HTTPException, Header, Query, Response
from pydantic import BaseModel, Field
from pymongo import MongoClient
import paho.mqtt.client as mqtt
from dotenv import load_dotenv

load_dotenv()
MONGODB_URI = os.getenv("MONGODB_URI")
MQTT_NAME = os.getenv("MQTT_NAME")

client = MongoClient(MONGODB_URI)
db = client.ecse3038
fans_col = db.fans
readings_col = db.readings
settings_col = db.settings

mqttc = mqtt.Client()
mqttc.connect("135.232.203.144", 1883)
mqttc.loop_start()

app = FastAPI()


class FanCreate(BaseModel):
    mac: str
    room: str


class Reading(BaseModel):
    mac: str
    humidity: float = Field(ge=0, le=100)


class Settings(BaseModel):
    mode: Literal["auto", "on", "off"]
    max_humidity: float = Field(ge=0, le=100)


# A1
@app.post("/fans", status_code=201)
def create_fan(fan: FanCreate):
    if fans_col.find_one({"mac": fan.mac}):
        raise HTTPException(409, "MAC already registered")
    key = secrets.token_urlsafe(16)
    fans_col.insert_one({"mac": fan.mac, "room": fan.room, "key": key})
    return {"mac": fan.mac, "room": fan.room, "key": key}

@app.get("/fans")
def list_fans():
    return list(fans_col.find({}, {"_id": 0, "key": 0}))

@app.get("/fans/{mac}")
def get_fan(mac: str):
    fan = fans_col.find_one({"mac": mac}, {"_id": 0, "key": 0})
    if not fan:
        raise HTTPException(404, "Unknown MAC")
    return fan


# A2
@app.post("/readings", status_code=201)
def create_reading(reading: Reading, x_api_key: str | None = Header(default=None)):
    fan = fans_col.find_one({"key": x_api_key})
    if not fan:
        raise HTTPException(401, "Missing or unissued key")
    if fan["mac"] != reading.mac:
        raise HTTPException(403, "Key does not match MAC")
    doc = {"mac": reading.mac, "humidity": reading.humidity,
           "time": datetime.now(timezone.utc)}
    readings_col.insert_one(doc)
    return {"mac": doc["mac"], "humidity": doc["humidity"], "time": doc["time"]}


# A3
@app.get("/readings")
def list_readings(mac: str | None = None, limit: int = Query(default=10, ge=1, le=100)):
    query = {"mac": mac} if mac else {}
    cursor = readings_col.find(query, {"_id": 0}).sort("time", -1).limit(limit)
    return list(cursor)


# A4
@app.put("/fans/{mac}/settings")
def set_settings(mac: str, settings: Settings):
    fan = fans_col.find_one({"mac": mac})
    if not fan:
        raise HTTPException(404, "Unknown MAC")
    existing = settings_col.find_one({"mac": mac})
    settings_col.update_one(
        {"mac": mac},
        {"$set": {"mode": settings.mode, "max_humidity": settings.max_humidity}},
        upsert=True,
    )
    topic = f"ecse3038/{MQTT_NAME}/fan/settings"
    payload = f'{{"mode": "{settings.mode}", "max_humidity": {settings.max_humidity}}}'
    mqttc.publish(topic, payload, retain=True)
    return Response(status_code=200 if existing else 201)


# A5
@app.delete("/fans/{mac}", status_code=204)
def delete_fan(mac: str, x_api_key: str | None = Header(default=None)):
    fan = fans_col.find_one({"mac": mac})
    if not fan:
        raise HTTPException(404, "Unknown MAC")
    owner = fans_col.find_one({"key": x_api_key})
    if not x_api_key or not owner:
        raise HTTPException(401, "Missing or unknown key")
    if owner["mac"] != mac:
        raise HTTPException(403, "Wrong fan's key")
    fans_col.delete_one({"mac": mac})
    return Response(status_code=204)