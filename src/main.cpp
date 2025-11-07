#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ESP32Servo.h>
#include <Arduino_JSON.h>
#include <WiFi.h>
#include <time.h>
#include <vector>
#include <algorithm>
#include <HTTPClient.h>
#include <DNSServer.h>

// Informations réseau Wi-Fi
const char* ssid = "RAZAFINDRAIBE";  
const char* password = "ALOary21BOX";  

// Déclaration des broches
const int IR_PIN_ENTRY = 33;
const int IR_PIN_EXIT = 32;
const int TRIG_PIN[3] = {14, 27, 26};  
const int ECHO_PIN[3] = {12, 25, 13};  
const int SERVO_PIN_ENTRY = 18;  
const int SERVO_PIN_EXIT = 19;   

Servo servoEntry, servoExit;     
AsyncWebServer server(80);       

// Historique des véhicules et liste des plaques autorisées
JSONVar vehicleHistory = JSONVar::parse("[]");  // Historique sous forme de tableau
std::vector<String> authorizedPlates;  // Plaques extraites du fichier JSON

// Gestion de session
bool isLoggedIn = false;

// Identifiants de connexion
const char* loginUser = "admin";
const char* loginPassword = "password";

// Fonction pour mesurer la distance avec les capteurs à ultrasons
float measureDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH);
  return (duration * 0.0343) / 2;
}

// Fonction pour récupérer l'heure actuelle
String getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String("Erreur");
  }
  char timeStr[30];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStr);
}

// Fonction pour ajouter un événement dans l'historique
void addToHistory(String plate, String entryTime, String exitTime = "") {
  JSONVar newEvent;
  newEvent["plate"] = plate;
  newEvent["entry"] = entryTime;
  newEvent["exit"] = exitTime;
  vehicleHistory[vehicleHistory.length()] = newEvent;
}

// Fonction pour mettre à jour l'heure de sortie dans l'historique
void updateExitTime(String plate, String exitTime) {
  for (int i = 0; i < vehicleHistory.length(); i++) {
    if (String((const char*)vehicleHistory[i]["plate"]) == plate && String((const char*)vehicleHistory[i]["exit"]) == "") {
      vehicleHistory[i]["exit"] = exitTime;
      break;
    }
  }
}

// Fonction pour charger la liste des plaques autorisées depuis le fichier JSON
void loadAuthorizedPlates() {
  File file = SPIFFS.open("/plates.json", "r");
  if (!file) {
    Serial.println("Erreur lors de l'ouverture du fichier JSON");
    return;
  }
  String content = file.readString();
  file.close();

  JSONVar data = JSON.parse(content);
  if (JSON.typeof(data) == "undefined") {
    Serial.println("Erreur lors de l'analyse JSON");
    return;
  }

  for (int i = 0; i < data.length(); i++) {
    authorizedPlates.push_back((const char*)data[i]);
  }
}

// Fonction pour envoyer une requête au serveur Python pour la reconnaissance de plaque
String sendPlateScanRequest(const char* url) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(url);  // URL du serveur Python
    int httpResponseCode = http.GET();  // Envoie une requête GET

    if (httpResponseCode == 200) {  // Si la requête est réussie
      String response = http.getString();
      http.end();  // Fermer la connexion HTTP
      return response;
    } else {
      Serial.println("Erreur lors de la requête HTTP : " + String(httpResponseCode));
      http.end();
      return "";
    }
  } else {
    Serial.println("Erreur de connexion Wi-Fi");
    return "";
  }
}

// Fonction pour compter les places de parking occupées
int countOccupiedSpots() {
  int occupiedCount = 0;
  for (int i = 0; i < 3; i++) {
    float distance = measureDistance(TRIG_PIN[i], ECHO_PIN[i]);
    if (distance < 10) {
      occupiedCount++;
    }
  }
  return occupiedCount;
}

// Fonction pour traiter la plaque détectée
void processDetectedPlate(String detectedPlate, Servo &servo, bool isEntry) {
  bool authorized = std::find(authorizedPlates.begin(), authorizedPlates.end(), detectedPlate) != authorizedPlates.end();

  if (authorized) {
    int occupiedSpots = countOccupiedSpots();

    if (isEntry && occupiedSpots >= 3) {
      Serial.println("Parking plein. Accès refusé pour la plaque : " + detectedPlate);
      return;
    }

    Serial.println("Accès autorisé pour la plaque : " + detectedPlate);
    servo.write(0);  // Ouvre la barrière

    String currentTime = getTime();

    if (isEntry) {
      addToHistory(detectedPlate, currentTime);
    } else {
      updateExitTime(detectedPlate, currentTime);
    }

    xTaskCreate([](void* servoPtr){
      Servo* servo = static_cast<Servo*>(servoPtr);
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      servo->write(90);  // Ferme la barrière
      vTaskDelete(NULL);
    }, "ServoTask", 2048, &servo, 1, NULL);
  } else {
    Serial.println("Accès refusé pour la plaque : " + detectedPlate);
  }
}

void setup() {
  Serial.begin(115200);

  // Connexion au réseau Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connexion au Wi-Fi...");
  }
  Serial.println("Wi-Fi connecté!");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());

  // Initialisation des capteurs et servos
  for (int i = 0; i < 3; i++) {
    pinMode(TRIG_PIN[i], OUTPUT);
    pinMode(ECHO_PIN[i], INPUT);
  }
  pinMode(IR_PIN_ENTRY, INPUT);
  pinMode(IR_PIN_EXIT, INPUT);

  servoEntry.attach(SERVO_PIN_ENTRY);
  servoExit.attach(SERVO_PIN_EXIT);
  servoEntry.write(90);  // Barrière fermée
  servoExit.write(90);   // Barrière fermée

  // Configuration NTP pour synchroniser l'heure (GMT+3)
  const long gmtOffset_sec = 3 * 3600;  // GMT+3
  const int daylightOffset_sec = 0;

  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");

  // Initialisation de SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Erreur de montage SPIFFS");
    return;
  }

  // Charger les plaques autorisées depuis le fichier JSON
  loadAuthorizedPlates();

  // Route pour la page de login
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/login.html", "text/html");
  });

  // Route pour traiter le login
  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("username", true) && request->hasParam("password", true)) {
      String username = request->getParam("username", true)->value();
      String password = request->getParam("password", true)->value();

      if (username == loginUser && password == loginPassword) {
        isLoggedIn = true;
        request->send(200, "text/plain", "Login successful");
      } else {
        request->send(401, "text/plain", "Invalid credentials");
      }
    } else {
      request->send(400, "text/plain", "Missing parameters");
    }
  });

  // Routes protégées
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
      request->redirect("/login");
      return;
    }
    request->send(SPIFFS, "/index.html", "text/html");
  });

  server.on("/status.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
      request->redirect("/login");
      return;
    }
    request->send(SPIFFS, "/status.html", "text/html");
  });

  server.on("/history.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
      request->redirect("/login");
      return;
    }
    request->send(SPIFFS, "/history.html", "text/html");
  });

  server.on("/manage.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
      request->redirect("/login");
      return;
    }
    request->send(SPIFFS, "/manage.html", "text/html");
  });

  server.on("/logout", HTTP_POST, [](AsyncWebServerRequest *request) {
    isLoggedIn = false;
    request->send(200, "text/plain", "Logged out");
  });

  // Route pour obtenir le statut des places de parking
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
      request->redirect("/login");
      return;
    }
    JSONVar status;
    for (int i = 0; i < 3; i++) {
      float distance = measureDistance(TRIG_PIN[i], ECHO_PIN[i]);
      status[String(i)] = (distance < 10) ? "Occupied" : "Available";
    }
    request->send(200, "application/json", JSON.stringify(status));
  });

  // Route pour obtenir l'historique des véhicules
  server.on("/history", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
      request->redirect("/login");
      return;
    }
    request->send(200, "application/json", JSON.stringify(vehicleHistory));
  });

  // Route pour ajouter une plaque autorisée
  server.on("/add_plate", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
      request->redirect("/login");
      return;
    }
    if (request->hasParam("plate", true)) {
      String newPlate = request->getParam("plate", true)->value();
      authorizedPlates.push_back(newPlate);
      request->send(200, "text/plain", "Plaque ajoutée: " + newPlate);
      Serial.println("Plaque ajoutée : " + newPlate);
    } else {
      request->send(400, "text/plain", "Paramètre 'plate' manquant");
    }
  });

  // Route pour supprimer une plaque autorisée
  server.on("/remove_plate", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
      request->redirect("/login");
      return;
    }
    if (request->hasParam("plate", true)) {
      String plateToRemove = request->getParam("plate", true)->value();
      authorizedPlates.erase(std::remove(authorizedPlates.begin(), authorizedPlates.end(), plateToRemove), authorizedPlates.end());
      request->send(200, "text/plain", "Plaque supprimée: " + plateToRemove);
      Serial.println("Plaque supprimée : " + plateToRemove);
    } else {
      request->send(400, "text/plain", "Paramètre 'plate' manquant");
    }
  });

  // Route pour vérifier les plaques détectées par OpenCV
  server.on("/check_plate", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
      request->redirect("/login");
      return;
    }
    if (request->hasParam("plate", true)) {
      String detectedPlate = request->getParam("plate", true)->value();
      
      bool authorized = std::find(authorizedPlates.begin(), authorizedPlates.end(), detectedPlate) != authorizedPlates.end();
      
      if (authorized) {
        int occupiedSpots = countOccupiedSpots();
        if (occupiedSpots >= 3) {
          request->send(403, "text/plain", "Parking plein. Accès refusé");
          return;
        }

        request->send(200, "text/plain", "Accès autorisé");
        servoEntry.write(0);  // Ouvre la barrière d'entrée

        xTaskCreate([](void*){
          vTaskDelay(5000 / portTICK_PERIOD_MS);
          servoEntry.write(90);  // Ferme la barrière d'entrée
          vTaskDelete(NULL);
        }, "ServoTask", 2048, NULL, 1, NULL);
      } else {
        request->send(403, "text/plain", "Accès refusé");
      }
    } else {
      request->send(400, "text/plain", "Paramètre 'plate' manquant");
    }
  });

  server.begin();
}

void loop() {
  if (digitalRead(IR_PIN_ENTRY) == HIGH) {
    Serial.println("Véhicule détecté à l'entrée, envoi d'une requête pour le scan...");
    String response = sendPlateScanRequest("http://192.168.1.149:5000/scan");
    if (response != "") {
      JSONVar jsonResponse = JSON.parse(response);
      String detectedPlate = (const char*)jsonResponse["plate"];
      processDetectedPlate(detectedPlate, servoEntry, true);  // true pour l'entrée
    }
    delay(5000);  // Pour éviter les déclenchements répétés
  }

  if (digitalRead(IR_PIN_EXIT) == HIGH) {
    Serial.println("Véhicule détecté à la sortie, envoi d'une requête pour le scan...");
    String response = sendPlateScanRequest("http://192.168.1.150:5000/scan");
    if (response != "") {
      JSONVar jsonResponse = JSON.parse(response);
      String detectedPlate = (const char*)jsonResponse["plate"];
      processDetectedPlate(detectedPlate, servoExit, false);  // false pour la sortie
    }
    delay(5000);  // Pour éviter les déclenchements répétés
  }
}