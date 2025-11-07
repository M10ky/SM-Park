import cv2
import requests

# Chargement de la webcam
cap = cv2.VideoCapture(0)

while True:
    ret, frame = cap.read()
    if not ret:
        break

    # Détection et extraction de la plaque ici...
    detected_plate = "ABC1234"  # Extrait via OpenCV (remplacer par le vrai code)

    # Envoi de la plaque au serveur ESP32
    url = "http://<esp32-ip>/check_plate"
    data = {'plate': detected_plate}
    response = requests.post(url, data=data)
    print(response.text)

    # Appuyez sur 'q' pour quitter
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cap.release()
cv2.destroyAllWindows()
