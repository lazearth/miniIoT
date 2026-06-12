// ==========================================
// 📌 1. ไลบรารีที่จำเป็น (Hardware Libraries)
// ==========================================
#include <Wire.h>              
#include <SPI.h>               
#include <LiquidCrystal_I2C.h> 
#include <Adafruit_AHTX0.h>    
#include <BH1750.h>            
#include <MFRC522.h>           

// ==========================================
// 📌 2. การกำหนดขาสัญญาณ (Pin Assignments)
// ==========================================
const int PUMP_RELAY_PIN = 14;   
const int FAN_RELAY_PIN = 27;    
const int MIST_RELAY_PIN = 26;  
const int LED_RELAY_PIN = 25;   

const int SOIL_PIN = 32;         
const int TRIG_PIN = 33;         
const int ECHO_PIN = 35;         
const int BUTTON_PIN = 13;       

// สัญญาณไฟจราจรหน้าตู้
const int TRAFFIC_RED_PIN = 2;    
const int TRAFFIC_YELLOW_PIN = 4;  
const int TRAFFIC_GREEN_PIN = 16;  
const int BUZZER_PIN = 15; 

#define SS_PIN 5                 
#define RST_PIN 17               

// สร้างออบเจกต์สำหรับควบคุมฮาร์ดแวร์
MFRC522 rfid(SS_PIN, RST_PIN);
Adafruit_AHTX0 aht;
LiquidCrystal_I2C lcd(0x27, 16, 2); 
BH1750 lightMeter;

// ==========================================
// 📌 3. ตัวแปรแกนหลักของระบบ (Core Variables)
// ==========================================
String currentMode = "MELON";            // โปรไฟล์พืชเริ่มต้น
float smoothedWaterDist = -1.0;          
int currentWaterPercent = 0;             
bool isWaterLow = false;                 
bool isPumping = false;                  
unsigned long pumpStartTime = 0;         
unsigned long lastAutoPump = 0;          
const unsigned long pumpDurationMs = 3000; // ระยะเวลาเปิดปั๊ม

const float tankDepthCm = 15.0;          
const float sensorOffsetCm = 5.0;        
const int alertWaterThreshold = 20;      

bool lastFanStatus = false;
bool lastMistStatus = false;

// ตัวแปรกู้ชีพฮาร์ดแวร์
bool pendingHardwareRecover = false; 

// ==========================================
// 📌 4. การตั้งค่าเริ่มต้นฮาร์ดแวร์ (Initialization)
// ==========================================
void setup() {
  Serial.begin(115200);

  pinMode(PUMP_RELAY_PIN, OUTPUT);
  pinMode(FAN_RELAY_PIN, OUTPUT);
  pinMode(MIST_RELAY_PIN, OUTPUT);
  pinMode(LED_RELAY_PIN, OUTPUT);
  
  pinMode(TRAFFIC_RED_PIN, OUTPUT);
  pinMode(TRAFFIC_YELLOW_PIN, OUTPUT);
  pinMode(TRAFFIC_GREEN_PIN, OUTPUT);
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Active LOW = HIGH คือปิด
  digitalWrite(PUMP_RELAY_PIN, HIGH); 
  digitalWrite(FAN_RELAY_PIN, HIGH);  
  digitalWrite(MIST_RELAY_PIN, HIGH);  
  digitalWrite(LED_RELAY_PIN, HIGH);  

  Wire.begin(); 
  lcd.init();
  lcd.backlight();
  aht.begin();
  lightMeter.begin();

  SPI.begin();
  rfid.PCD_Init();
  
  Serial.println("🚀 [SYSTEM READY] ระบบนิเวศพร้อมทำงาน!");
}

// ==========================================
// 📌 5. ฟังก์ชันสั่งรดน้ำ (Hardware Safety Lock)
// ==========================================
void activatePump(String source) {
  if (isPumping) return; 

  if (isWaterLow) {
    Serial.println("🚨 [HARDWARE LOCK] ปฏิเสธการรดน้ำ! น้ำในแท็งก์ต่ำเกินไป");
    return; 
  }

  isPumping = true;
  pumpStartTime = millis(); 
  digitalWrite(PUMP_RELAY_PIN, LOW); // เปิดปั๊ม
}

// ==========================================
// 📌 6. สมองกลควบคุมระบบนิเวศ (Auto Control Hysteresis)
// ==========================================
void autoControlEcosystem(float currentTemp, float currentHum, int currentSoil) {
  // ดึงค่าสถานะเดิมมาใช้ตั้งต้น เพื่อให้ระบบหน่วงตัวเองได้
  bool fanAuto = lastFanStatus;
  bool mistAuto = lastMistStatus;

  // 🧠 ประมวลผลแบบ Hysteresis (หน่วงอุณหภูมิ/ความชื้น ป้องกันรีเลย์พัง)
  if (currentMode == "MELON") {
    // พัดลม (ช่วงหน่วง 1.5 องศา)
    if (currentTemp >= 32.0) fanAuto = true;
    else if (currentTemp <= 30.5) fanAuto = false;
    
    // พ่นหมอก (ช่วงหน่วง 5%)
    if (currentHum <= 60.0) mistAuto = true;
    else if (currentHum >= 65.0) mistAuto = false;
    
    if (!isWaterLow && currentSoil < 40 && !isPumping && (millis() - lastAutoPump > 60000)) {
      activatePump("AUTO"); 
      lastAutoPump = millis(); 
    }
  } 
  else if (currentMode == "SALAD") {
    if (currentTemp >= 28.0) fanAuto = true;
    else if (currentTemp <= 26.5) fanAuto = false;
    
    if (currentHum <= 75.0) mistAuto = true;
    else if (currentHum >= 80.0) mistAuto = false;
    
    if (!isWaterLow && currentSoil < 60 && !isPumping && (millis() - lastAutoPump > 60000)) {
      activatePump("AUTO"); 
      lastAutoPump = millis(); 
    }
  }

  lastFanStatus = fanAuto;
  lastMistStatus = mistAuto;

  // ⚙️ สั่งงานรีเลย์พัดลมและเครื่องพ่นหมอก
  digitalWrite(FAN_RELAY_PIN, fanAuto ? LOW : HIGH);
  digitalWrite(MIST_RELAY_PIN, mistAuto ? LOW : HIGH);
}

// ==========================================
// 📌 7. ลูปการทำงานหลัก (Main Event Loop)
// ==========================================
void loop() {
  
  // ---------------------------------------------------------
  // 7.1 ระบบกู้ชีพฮาร์ดแวร์ (Hardware Watchdog)
  // ---------------------------------------------------------
  if (pendingHardwareRecover) {
    pendingHardwareRecover = false;
    delay(50); // รอคลื่นรบกวนสงบ
    
    Wire.begin();    
    lcd.init();      
    SPI.begin();
    rfid.PCD_Init(); 
  }

  // ---------------------------------------------------------
  // 7.2 ตัวจับเวลาปิดปั๊มและกระตุ้นระบบกู้ชีพ
  // ---------------------------------------------------------
  if (isPumping && (millis() - pumpStartTime >= pumpDurationMs)) {
    digitalWrite(PUMP_RELAY_PIN, HIGH); // ดับปั๊ม
    isPumping = false;
    pendingHardwareRecover = true; // 🌟 ตะโกนเรียกบล็อกกู้ชีพในรอบถัดไป
  }

  // ---------------------------------------------------------
  // 7.3 ระบบคำนวณระดับน้ำอัจฉริยะ (EMA Filter)
  // ---------------------------------------------------------
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  float rawDist = pulseIn(ECHO_PIN, HIGH, 30000) * 0.034 / 2;

  if (rawDist > 0.0 && rawDist <= 400.0) {
    if (smoothedWaterDist < 0) smoothedWaterDist = rawDist; 
    else smoothedWaterDist = (0.2 * rawDist) + (0.8 * smoothedWaterDist); 
    
    float totalRange = tankDepthCm - sensorOffsetCm; 
    float waterHeight = tankDepthCm - smoothedWaterDist; 
    int rawPercent = (waterHeight / max(totalRange, 1.0f)) * 100;
    
    currentWaterPercent = (constrain(rawPercent, 0, 100) / 10) * 10; 
    isWaterLow = (currentWaterPercent <= alertWaterThreshold);
  }

  // ---------------------------------------------------------
  // 7.4 อ่านค่าเซนเซอร์และโยนให้สมองกลตัดสินใจ
  // ---------------------------------------------------------
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  int soilPercent = map(analogRead(SOIL_PIN), 2535, 830, 0, 100);
  
  autoControlEcosystem(temp.temperature, humidity.relative_humidity, constrain(soilPercent, 0, 100));

  // ---------------------------------------------------------
  // 7.5 ควบคุมไฟจราจรหน้าตู้ (Non-blocking Blink)
  // ---------------------------------------------------------
  if (currentWaterPercent <= alertWaterThreshold) {
      digitalWrite(TRAFFIC_RED_PIN, HIGH); 
      digitalWrite(TRAFFIC_YELLOW_PIN, LOW); 
      digitalWrite(TRAFFIC_GREEN_PIN, LOW); 
  } 
  else if (currentWaterPercent <= 60) {
      digitalWrite(TRAFFIC_RED_PIN, LOW); 
      digitalWrite(TRAFFIC_YELLOW_PIN, HIGH); 
      digitalWrite(TRAFFIC_GREEN_PIN, LOW); 
  } 
  else {
      digitalWrite(TRAFFIC_RED_PIN, LOW); 
      digitalWrite(TRAFFIC_YELLOW_PIN, LOW); 
      digitalWrite(TRAFFIC_GREEN_PIN, HIGH); 
  }

  // ---------------------------------------------------------
  // 7.6 ระบบจัดการสิทธิ์ผ่าน RFID (จำลองโครงสร้าง)
  // ---------------------------------------------------------
  /*
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String cardID = // แปลงรหัสบัตร
    if (cardID == MASTER_CARD_UID) {
       // ปลดล็อกระบบ
    } else {
       // ส่งคิวไปให้ Core 0 เช็กสิทธิ์ที่ Server
    }
  }
  */
}
