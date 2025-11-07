from flask import Flask, jsonify
import cv2
import easyocr
import numpy as np
import logging

app = Flask(__name__)

# Configurer les logs pour suivre l'exécution
logging.basicConfig(level=logging.INFO)

# Créer une instance du lecteur EasyOCR
reader = easyocr.Reader(['en'], gpu=False)  # 'gpu=True' si vous voulez utiliser le GPU pour accélérer

# Fonction de détection de plaque et OCR avec OpenCV et EasyOCR
def recognize_plate():
    # Démarrer la capture vidéo (webcam)
    cap = cv2.VideoCapture(0)
    
    if not cap.isOpened():
        logging.error("Erreur : Impossible d'ouvrir la caméra")
        return "Erreur : Impossible d'ouvrir la caméra"
    
    # Capturer une image
    ret, frame = cap.read()
    if not ret:
        logging.error("Erreur : Impossible de capturer l'image")
        return "Erreur : Impossible de capturer l'image"
    
    cv2.imwrite("captured_image.jpg", frame)
    logging.info("Image capturée et sauvegardée sous 'captured_image.jpg'")
    
    # Convertir en niveaux de gris pour améliorer la détection
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    
    # Appliquer des filtres pour améliorer la reconnaissance
    gray = cv2.bilateralFilter(gray, 11, 17, 17)
    
    # Appliquer Canny pour détecter les bords
    edged = cv2.Canny(gray, 30, 200)
    
    # Trouver les contours
    contours, _ = cv2.findContours(edged, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
    contours = sorted(contours, key=cv2.contourArea, reverse=True)[:10]
    
    plate = None
    for contour in contours:
        peri = cv2.arcLength(contour, True)
        approx = cv2.approxPolyDP(contour, 0.02 * peri, True)
        if len(approx) == 4:  # Si c'est un quadrilatère, on suppose que c'est une plaque
            plate = approx
            break
    
    if plate is None:
        logging.error("Erreur : Aucune plaque détectée")
        return "Erreur : Aucune plaque détectée"
    
    # Extraire la région de la plaque d'immatriculation
    mask = np.zeros(gray.shape, dtype="uint8")
    cv2.drawContours(mask, [plate], -1, 255, -1)
    (x, y) = np.where(mask == 255)
    roi = gray[min(x):max(x), min(y):max(y)]
    
    # Sauvegarder l'image de la plaque extraite
    cv2.imwrite("plate_image.jpg", roi)
    
    # Utiliser EasyOCR pour l'OCR
    try:
        result = reader.readtext(roi, detail=0)  # 'detail=0' ne renvoie que le texte détecté
        if len(result) == 0:
            logging.error("Erreur : La reconnaissance OCR a échoué")
            return "Erreur : La reconnaissance OCR a échoué"
        
        detected_plate = result[0]  # On prend le premier résultat reconnu
    except Exception as e:
        logging.error(f"Erreur : {str(e)}")
        return "Erreur : La reconnaissance OCR a échoué"
    
    cap.release()
    cv2.destroyAllWindows()
    
    return detected_plate

@app.route('/scan', methods=['GET'])
def scan_plate():
    logging.info("Requête reçue pour la reconnaissance de plaque")
    
    detected_plate = recognize_plate()
    
    if "Erreur" in detected_plate:
        logging.error(f"Erreur lors de la reconnaissance de la plaque : {detected_plate}")
        return jsonify({'error': detected_plate}), 500
    
    response = {
        'plate': detected_plate
    }
    logging.info(f"Plaque détectée : {detected_plate}")
    return jsonify(response)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
