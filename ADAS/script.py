import cv2
import numpy as np
import time
import serial
from ultralytics import YOLO

# -----------------------------
# SERIAL SETUP
# -----------------------------
PORT ="/dev/cu.usbmodem3C8427C3DC082"          # change this to your Arduino port
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(2)  # give Arduino time to reset

# -----------------------------
# YOLO SETUP
# -----------------------------
model = YOLO("yolov8n.pt")
cap = cv2.VideoCapture("input2.mp4")

K = 800
objects = {}

# avoid spamming same command continuously
last_sent_code = None
last_sent_time = 0
SEND_COOLDOWN = 2.0  # seconds

def send_code(code):
    global last_sent_code, last_sent_time
    now = time.time()
    if code is None:
        return
    if code == last_sent_code and (now - last_sent_time) < SEND_COOLDOWN:
        return
    ser.write((code + "\n").encode())
    last_sent_code = code
    last_sent_time = now
    print(f"[PYTHON -> ARDUINO] Sent: {code}")

while True:
    ret, frame = cap.read()
    if not ret:
        break

    frame = cv2.resize(frame, (640, 360))
    h, w = frame.shape[:2]
    current_time = time.time()

    # TRIANGLE
    triangle = np.array([
        [0, h],
        [w, h],
        [w // 2, int(h * 0.5)]
    ])

    overlay = frame.copy()
    cv2.fillPoly(overlay, [triangle], (0, 255, 0))
    output = cv2.addWeighted(overlay, 0.3, frame, 0.7, 0)

    # CENTER LINE
    center_x = w // 2
    cv2.line(output, (center_x, 0), (center_x, h), (255, 0, 255), 2)

    # YOLO TRACK
    results = model.track(frame, persist=True, imgsz=320, verbose=False)

    for r in results:
        if r.boxes is None:
            continue

        for box in r.boxes:
            cls = int(box.cls[0])
            conf = float(box.conf[0])
            label = model.names[cls]

            if label not in ["car", "truck", "bus", "motorbike"]:
                continue

            if conf < 0.4:
                continue

            if box.id is None:
                continue

            obj_id = int(box.id[0])
            x1, y1, x2, y2 = map(int, box.xyxy[0])

            bbox_height = y2 - y1
            if bbox_height <= 0:
                continue

            distance = K / bbox_height

            # -------------------------------
            # OBJECT STATE MACHINE
            # -------------------------------
            if obj_id not in objects:
                objects[obj_id] = {
                    "start_time": current_time,
                    "d_t": None,
                    "d_t1": None,
                    "probable": False,
                    "alerted": False
                }

            obj = objects[obj_id]
            elapsed = current_time - obj["start_time"]

            if obj["d_t"] is None and elapsed >= 0.5:
                obj["d_t"] = distance

            if obj["d_t"] is not None and obj["d_t1"] is None and elapsed >= 1.5:
                obj["d_t1"] = distance

            if obj["d_t"] is not None and obj["d_t1"] is not None and not obj["probable"]:
                if obj["d_t1"] - obj["d_t"] < 0:
                    obj["probable"] = True

            # POSITION ANALYSIS
            cx = (x1 + x2) // 2
            cy = (y1 + y2) // 2

            if cx < center_x:
                side = "CAR RIGHT"
                color = (255, 0, 0)
                approach_dir = "FROM RIGHT"
                code = "R"
            else:
                side = "CAR LEFT"
                color = (0, 0, 255)
                approach_dir = "FROM LEFT"
                code = "L"

            inside_triangle = cv2.pointPolygonTest(triangle, (cx, cy), False) >= 0

            if distance < 5:
                proximity = "VERY CLOSE"
            elif distance < 10:
                proximity = "CLOSE"
            else:
                proximity = "FAR"

            # -------------------------------
            # CONFIRMED ALERT
            # -------------------------------
            if obj["probable"] and not obj["alerted"] and proximity == "CLOSE":
                if inside_triangle:
                    print(f"[ALERT] Vehicle ID {obj_id} APPROACHING FROM BEHIND | Distance={distance:.2f} m")
                    send_code("B")
                else:
                    print(f"[ALERT] Vehicle ID {obj_id} APPROACHING {approach_dir} | Distance={distance:.2f} m")
                    send_code(code)

                obj["alerted"] = True

            # DRAW
            cv2.rectangle(output, (x1, y1), (x2, y2), color, 2)
            cv2.putText(
                output,
                f"{side} | {distance:.1f}m | {proximity}",
                (x1, y1 - 5),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.5, color, 2
            )

    cv2.putText(output, "CAR RIGHT", (40, h - 20),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 0, 0), 2)

    cv2.putText(output, "CAR LEFT", (w - 150, h - 20),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)

    cv2.putText(output, "CENTER (REAR AXIS)", (center_x - 110, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 0, 255), 1)

    cv2.imshow("Discrete Distance Sampling", output)

    if cv2.waitKey(1) & 0xFF == 27:
        break

cap.release()
ser.close()
cv2.destroyAllWindows()
