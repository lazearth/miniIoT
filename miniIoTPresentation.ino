// ==========================================
// 📌 1. ไลบรารีที่จำเป็น (Hardware Libraries)
// ==========================================
#include <Wire.h>              // สำหรับการสื่อสาร I2C (หน้าจอ, อุณหภูมิ, แสง)
#include <SPI.h>               // สำหรับการสื่อสาร SPI (เครื่องอ่าน RFID)
#include <LiquidCrystal_I2C.h> // ควบคุมหน้าจอ LCD
#include <Adafruit_AHTX0.h>    // เซนเซอร์อุณหภูมิ/ความชื้น (AHT10)
#include <BH1750.h>            // เซนเซอร์แสง
#include <MFRC522.h>           // เครื่องอ่านบัตร RFID

// ==========================================
// 📌 2. การกำหนดขาสัญญาณ (Pin Assignments)
// ==========================================
const int PUMP_RELAY_PIN = 14;   
const int FAN_RELAY_PIN = 27;    
const int MIST_RELAY_PIN = 26;  
const int LED_RELAY_PIN = 25;   

const int SOIL_PIN = 32;         // เซนเซอร์ความชื้นดิน (Analog)
const int TRIG_PIN = 33;         // ขาส่งคลื่น Ultrasonic
const int ECHO_PIN = 35;         // ขารับคลื่น Ultrasonic
ฺconst int BUTTON_PIN = 13;       // ขาปุ่มกด

#define SS_PIN 5                 // ขาเลือกอุปกรณ์ SPI (RFID)
#define RST_PIN 17               // ขารีเซ็ต RFID

// สร้างออบเจกต์สำหรับควบคุมฮาร์ดแวร์
MFRC522 rfid(SS_PIN, RST_PIN);
Adafruit_AHTX0 aht;
LiquidCrystal_I2C lcd(0x27, 16, 2); 
BH1750 lightMeter;

// ==========================================
// 📌 3. ตัวแปรแกนหลักของระบบ (Core Variables)
// ==========================================
String currentMode = "MELON";            // โปรไฟล์พืชเริ่มต้น
float smoothedWaterDist = -1.0;          // ค่าความห่างผิวน้ำที่ผ่านการกรอง (EMA)
int currentWaterPercent = 0;             // ระดับน้ำในแท็งก์ (%)
bool isWaterLow = false;                 // ธงแจ้งเตือนน้ำต่ำ
bool isPumping = false;                  // ธงสถานะปั๊มน้ำ
unsigned long pumpStartTime = 0;         // จับเวลารดน้ำ
unsigned long lastAutoPump = 0;          // หน่วงเวลารดน้ำอัตโนมัติ

const float tankDepthCm = 15.0;          // ความลึกแท็งก์
const float sensorOffsetCm = 5.0;        // ระยะเผื่อของเซนเซอร์
const int alertWaterThreshold = 20;      // เกณฑ์น้ำวิกฤต (ป้องกันปั๊มไหม้)

// ==========================================
// 📌 4. การตั้งค่าเริ่มต้นฮาร์ดแวร์ (Initialization)
// ==========================================
void setup() {
  Serial.begin(115200);

  // กำหนดทิศทางของขาสัญญาณ (I/O)
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  pinMode(FAN_RELAY_PIN, OUTPUT);
  pinMode(MIST_RELAY_PIN, OUTPUT);
  pinMode(LED_RELAY_PIN, OUTPUT);
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // สั่งปิดรีเลย์ทุกตัวเพื่อความปลอดภัยตอนเปิดเครื่อง (Active LOW)
  digitalWrite(PUMP_RELAY_PIN, HIGH); 
  digitalWrite(FAN_RELAY_PIN, HIGH);  
  digitalWrite(MIST_RELAY_PIN, HIGH);  
  digitalWrite(LED_RELAY_PIN, HIGH);  

  // เริ่มต้นการสื่อสาร I2C
  Wire.begin(); 
  lcd.init();
  lcd.backlight();
  aht.begin();
  lightMeter.begin();

  // เริ่มต้นการสื่อสาร SPI
  SPI.begin();
  rfid.PCD_Init();
  
  Serial.println("🚀 [SYSTEM READY] ระบบนิเวศพร้อมทำงาน!");
}

// ==========================================
// 📌 5. ฟังก์ชันป้องกันปั๊มไหม้ (Hardware Safety Lock)
// ==========================================
void activatePump(String source) {
  if (isPumping) return; // ป้องกันคำสั่งซ้อน

  if (isWaterLow) {
    Serial.println("🚨 [HARDWARE LOCK] ปฏิเสธการรดน้ำ! น้ำในแท็งก์ต่ำเกินไป");
    return; // เตะออกจากฟังก์ชันทันที
  }

  isPumping = true;
  pumpStartTime = millis(); 
  digitalWrite(PUMP_RELAY_PIN, LOW); // เปิดปั๊ม
}

// ==========================================
// 📌 6. สมองกลควบคุมระบบนิเวศ (Auto Control Ecosystem)
// ==========================================
void autoControlEcosystem(float currentTemp, float currentHum, int currentSoil) {
  bool fanAuto = false;
  bool mistAuto = false;

  // 🧠 ประมวลผลตาม Profile พืชที่กำลังเลือกอยู่
  if (currentMode == "MELON") {
    if (currentTemp > 32.0) fanAuto = true;
    if (currentHum < 60.0) mistAuto = true;
    
    if (!isWaterLow && currentSoil < 40 && !isPumping && (millis() - lastAutoPump > 60000)) {
      activatePump("AUTO"); 
      lastAutoPump = millis(); 
    }
  } 
  else if (currentMode == "SALAD") {
    if (currentTemp > 28.0) fanAuto = true;
    if (currentHum < 75.0) mistAuto = true;
    
    if (!isWaterLow && currentSoil < 60 && !isPumping && (millis() - lastAutoPump > 60000)) {
      activatePump("AUTO"); 
      lastAutoPump = millis(); 
    }
  }

  // ⚙️ สั่งงานรีเลย์พัดลมและเครื่องพ่นหมอก
  digitalWrite(FAN_RELAY_PIN, fanAuto ? LOW : HIGH);
  digitalWrite(MIST_RELAY_PIN, mistAuto ? LOW : HIGH);
}

// ==========================================
// 📌 7. ลูปการทำงานหลัก (Main Event Loop)
// ==========================================
void loop() {
  
  // ---------------------------------------------------------
  // 7.1 ระบบคำนวณระดับน้ำอัจฉริยะ (Smart Water Level & EMA Filter)
  // ---------------------------------------------------------
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  float rawDist = pulseIn(ECHO_PIN, HIGH, 30000) * 0.034 / 2;

  if (rawDist > 0.0 && rawDist <= 400.0) {
    // 📈 EMA Filter ลดการสวิงของตัวเลขเวลาผิวน้ำกระเพื่อม
    if (smoothedWaterDist < 0) smoothedWaterDist = rawDist; 
    else smoothedWaterDist = (0.2 * rawDist) + (0.8 * smoothedWaterDist); 
    
    // 🧮 สมการเทียบบัญญัติไตรยางศ์ (Dynamic Calculation)
    float totalRange = tankDepthCm - sensorOffsetCm; 
    float waterHeight = tankDepthCm - smoothedWaterDist; 
    int rawPercent = (waterHeight / max(totalRange, 1.0f)) * 100;
    
    currentWaterPercent = (constrain(rawPercent, 0, 100) / 10) * 10; 
    isWaterLow = (currentWaterPercent <= alertWaterThreshold);
  }

  // ---------------------------------------------------------
  // 7.2 อ่านค่าเซนเซอร์และโยนให้สมองกลตัดสินใจ
  // ---------------------------------------------------------
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  int soilPercent = map(analogRead(SOIL_PIN), 2535, 830, 0, 100);
  
  autoControlEcosystem(temp.temperature, humidity.relative_humidity, constrain(soilPercent, 0, 100));

  // ---------------------------------------------------------
  // 7.3 ระบบจัดการสิทธิ์และโปรไฟล์ผ่าน RFID (Role-Based Access)
  // ---------------------------------------------------------
  // (โค้ดจำลองสถานการณ์เมื่อได้รับการตอบกลับจากเซิร์ฟเวอร์)
  /*
  String role = doc["role"].as<String>(); 
  
  if (role == "ADMIN_UNLOCK") {
    // 🗝️ แจกกุญแจให้ Admin รดน้ำแมนนวลได้ 1 นาที
    rfidUnlockTime = millis();
  } else {
    // 🌱 เปลี่ยนพืชทั้งตู้เพียงแค่แตะบัตร Profile Card
    currentMode = role; 
  }
  */
}