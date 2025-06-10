import paho.mqtt.client as mqtt
import json
import hashlib

# Configuración MQTT para Mosquitto
MOSQUITTO_SERVER = "test.mosquitto.org"
MOSQUITTO_PORT = 1883  # Puerto estándar para MQTT sin TLS
MOSQUITTO_FEED = "testMQTT_SIM800"

# Función para calcular SHA-256 de un JSON con "sha256": "0"
def calculate_sha256(json_data):
    temp_json = json_data.copy()
    temp_json["sha256"] = "0"  # Restaurar el campo a "0"
    json_str = json.dumps(temp_json, separators=(',', ':'), sort_keys=False)
    return hashlib.sha256(json_str.encode()).hexdigest()

# Función para verificar que los timestamps sean consecutivos
def check_timestamps(data):
    timestamps = [int(entry["timestamp"]) for entry in data]
    return all((timestamps[i] - timestamps[i-1]) > 0 for i in range(1, len(timestamps)))

# Función para verificar que temperatura y humedad no sean nan
def check_temphumid(data):
    temps = [entry["temp"] for entry in data]
    humids = [entry["humid"] for entry in data]
    return all(temps[i] != "nan" and humids[i] != "nan" for i in range(1,len(temps)))

# Callback cuando se recibe un mensaje MQTT
def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8")
        print(f"JSON Recibido:\n{payload}")  # Muestra el JSON crudo
        
        json_data = json.loads(payload)

        # Validar cantidad de entradas
        if "data" in json_data and len(json_data["data"]) == 6:
            print("[✔] JSON contiene 6 entradas.")

            # Validar timestamps consecutivos
            if check_timestamps(json_data["data"]):
                print("[✔] Timestamps correctos y consecutivos.")
            else:
                print("[❌] Timestamps incorrectos.")

            # Validar hash SHA-256
            expected_hash = calculate_sha256(json_data)
            if json_data["sha256"] == expected_hash:
                print("[✔] Hash SHA-256 válido.")
            else:
                print("[❌] Hash SHA-256 incorrecto.")
                print(f"Esperado: {expected_hash}")
                print(f"Recibido: {json_data['sha256']}")

            # Validar temperatura y humedad distintos de nan
            if check_temphumid(json_data["data"]):
                print("[✔] humedad y temperatura correctos.")
            else:
                print("[❌] humedad y temperatura incorrectos.")
        else:
            print("[❌] JSON no tiene 6 entradas.")

    except Exception as e:
        print(f"[❌] Error procesando JSON: {e}")

# Configuración del cliente MQTT
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
# Con Mosquitto público, no se requiere usuario ni contraseña, ni TLS para el puerto 1883
# client.username_pw_set(AIO_USERNAME, AIO_KEY) # Ya no es necesario
client.on_message = on_message
# client.tls_set()  # No es necesario para test.mosquitto.org en el puerto 1883

print("Conectando a MQTT...")
client.connect(MOSQUITTO_SERVER, MOSQUITTO_PORT, 300)
client.subscribe(MOSQUITTO_FEED)
print(f"Suscrito a {MOSQUITTO_FEED}")

# Bucle para mantener el script corriendo
client.loop_forever()