
#include <OneWire.h>            //Librairie du bus OneWire
#include <DallasTemperature.h>  //Librairie du capteur
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

/* Conf blynk */
#define BLYNK_TOKEN "XXXX"
#define WIFI_SSID   "XXXX" 
#define WIFI_PWD    "XXXX"
/* GPIO */
#define GPIO_BP     0
#define GPIO_LED    13
#define GPIO_SWITCH 12
#define GPIO_TEMP   14

/* L'état de la led est inversé */
#define LED_HIGH    LOW 
#define LED_LOW     HIGH

/* Capteur température */
OneWire oneWire(GPIO_TEMP); 
DallasTemperature sensors(&oneWire); 
DeviceAddress sensorDeviceAddress;


/* Variables internes */
float   Temp_val ;
float   Temp_consigne = 0 ;
float   Temp_delta = 0.5;

int     BP_val = LOW ;
int     Relay_val = LOW ;

/* Time */
unsigned long timeNow = 0;
unsigned long timeLast = 0;
int seconds = 0;
int minutes = 0;
int hours = 0;

int minutes_etape = 0 ;
int minutes_all = 0 ;


/* Parametres */
int EmpatageChauffeTemp = 0 ;
int EmpatageMaintienTemp = 0 ;
int EmpatageMaintienDuree_min = 0 ;

int ChauffeTemp = 0 ;
int ChauffeDuree_min = 0 ;

  

int StartStop = false ; 

/* Datas Blynk */
#define DATA_OUT_TEMP               V0
#define DATA_OUT_CONSIGNE           V1
#define DATA_OUT_RELAY              V2
#define DATA_OUT_TEMP_GRAPH         V3
#define DATA_OUT_STAGE              V4
#define DATA_OUT_LCD                V5
#define DATA_MINUTES_ALL            V6
#define DATA_MINUTES_ETAT           V7

#define DATE_IN_START_STOP          V10

#define DATE_IN_C_EMPATAGE_CHAUFFE  V20
#define DATE_IN_C_EMPATAGE_MAINTIEN V21
#define DATE_IN_C_EMPATAGE_TEMPS    V22
#define DATE_IN_C_CHAUFFE           V23
#define DATE_IN_C_CHAUFFE_TEMPS     V24

unsigned long timeBlynkLast = 0 ;
#define BLYNK_UPDATE_SEC              2


WidgetLED ledRelay(DATA_OUT_RELAY);
WidgetLCD lcd(DATA_OUT_LCD);
String LCD1 = "";
String LCD2 = "";


/* Machine état brassage */
typedef enum
{
    ETAT_INIT=0,
    ETAT_EMPATAGE_CHAUFFE,
    ETAT_EMPATAGE_USER1,
    ETAT_EMPATAGE,
    ETAT_EMPATAGE_USER2,
    ETAT_CHAUFFE,
    ETAT_FIN
}tEtat ;

tEtat eEtatCourant = ETAT_INIT ;



void setup(void)
{
    Serial.begin(115200);
    Serial.println("Init ...");

    /* Configuration GPIO */
    pinMode(GPIO_LED, OUTPUT);
    pinMode(GPIO_SWITCH, OUTPUT);
    pinMode(GPIO_BP, INPUT);
    pinMode(GPIO_TEMP, INPUT);

    LedOn();
    sensors.begin();                                  //Activation des capteurs
    sensors.getAddress(sensorDeviceAddress, 0);       //Demande l'adresse du capteur à l'index 0 du bus
    sensors.setResolution(sensorDeviceAddress, 10);   //Résolutions possibles: 9,10,11,12 -> 10 = 0.25°C

    Blynk.begin(BLYNK_TOKEN,WIFI_SSID,WIFI_PWD);
    InitTime();

    LedOff();
    RelayOff();
    Serial.println("Init Ok");
}


void loop(void)
{
  //  Serial.println("******");
  MajTime();
  
  /* Lecure BP */
  BP_val = LectureBouton();

  /* Lecture température */
  Temp_val = LectureTemperature();

  /* */
  MachineEtat();

  /* Update blynk variables */
  BlynkUpdate();
}



void MachineEtat(void)
{
    switch (eEtatCourant)
    {
      case ETAT_INIT :
        LCD1 = "Set Conf";
        LCD2 = "+ START";
        
        RelayOff();
        Temp_consigne = 0 ;
        
        if (0 != StartStop)
        {
            /* Raz minutes */
            minutes_etape = 0 ;
            minutes_all = 0 ;
            RelayOn();
            Temp_consigne = EmpatageChauffeTemp;
            eEtatCourant = ETAT_EMPATAGE_CHAUFFE ;
        }
        break ;
        
      case ETAT_EMPATAGE_CHAUFFE :
        LCD1 = "Empatage";
        LCD2 = "Chauffe en cours ...";
        if (Temp_val >= (Temp_consigne + Temp_delta))
        {
            /* Raz minutes */
            minutes_etape = 0 ;
            RelayOff();
            eEtatCourant = ETAT_EMPATAGE_USER1 ;
            Blynk.notify("Empatage : Fin chauffage eau");
        } 
        break ;
      
      case ETAT_EMPATAGE_USER1 :
        LCD1 = "Empatage Ok";
        LCD2 = "BP to continue"; 
        if (HIGH == BP_val)
        {
            /* Raz minutes */
            minutes_etape = 0 ;
            Temp_consigne = EmpatageMaintienTemp ;
            eEtatCourant = ETAT_EMPATAGE ;
        }
        break ;
        
    case ETAT_EMPATAGE :
        LCD1 = "Empatage";
        LCD2 = "En cours ..."; 
        if (Temp_val <= (Temp_consigne + Temp_delta))
        {
            LCD1 = "Temp. basse";
            LCD2 = "Ajouter eau";
            Blynk.notify("Empatage : Température trop basse");
        }

        if (minutes_etape >= EmpatageMaintienDuree_min)
        {
              /* Raz minutes */
              minutes_etape = 0 ;
              eEtatCourant = ETAT_EMPATAGE_USER2 ;
              Blynk.notify("Empatage : Fin");
        }
        break ;
      
    case ETAT_EMPATAGE_USER2 :
        LCD1 = "Empatage Ok";
        LCD2 = "BP to continue"; 
        if (HIGH == BP_val)
        {
             /* Raz minutes */
              minutes_etape = 0 ;
              Temp_consigne = ChauffeTemp ;
              eEtatCourant = ETAT_CHAUFFE ;
        }
        break ;
        
    case ETAT_CHAUFFE :
        LCD1 = "Chauffe";
        LCD2 = "En cours ..."; 
        /* Température >= consigne */
        if (Temp_val >= (Temp_consigne + Temp_delta))
        {
            RelayOff();     
        }
        else if  (Temp_val <= (Temp_consigne - Temp_delta))
        {
            RelayOn();
        }
        else
        {}

        /* Durée Max */
        if (minutes_etape >= ChauffeDuree_min)
        {
              RelayOff(); 
              eEtatCourant = ETAT_FIN ;
              Blynk.notify("Brassage terminé !!!!");
        }
        break ;
        
     case ETAT_FIN :
        LCD1 = "Brassage";
        LCD2 = "Terminé !!!"; 
        break ;
       
     default : 
      break ; 
    }

    if ((0 == StartStop)&&(ETAT_INIT != eEtatCourant))
    {
        Serial.println("STOP");
        eEtatCourant = ETAT_INIT ;
    }
}


/****************************************
* Température
*/

float LectureTemperature(void)
{
    sensors.requestTemperatures(); //Demande la température aux capteurs
    return sensors.getTempCByIndex(0); 
}


/****************************************
* Relay
*/

void RelayOn(void)
{
    digitalWrite(GPIO_SWITCH, HIGH);
    Relay_val = HIGH ;
}

void RelayOff(void)
{
    digitalWrite(GPIO_SWITCH, LOW);
    Relay_val = LOW ;
}


/****************************************
* LED
*/

void LedOn(void)
{
    digitalWrite(GPIO_LED,LED_HIGH);
}

void LedOff(void)
{
    digitalWrite(GPIO_LED,LED_LOW);
}

/****************************************
* Bouton Poussoir
*/

int LectureBouton(void)
{
    int val ;
    val = digitalRead(GPIO_BP);
    if(HIGH == val)
    {
        val = LOW  ;
        LedOff();
    }
    else
    {
        val = HIGH ;
        LedOn();
    }
    /*Serial.print("BP : ");
    Serial.println(val);*/
    return val ;
}


/****************************************
* Gestion Temp
*/

void InitTime(void)
{
    Serial.println("Init Time");
    timeLast =  millis()/1000;
}

void MajTime(void)
{
    timeNow = millis() / 1000; // the number of milliseconds that have passed since boot
    seconds = timeNow - timeLast;//the number of seconds that have passed since the last time 60 seconds was reached.
    if (seconds >= 60) 
    {
        timeLast = timeNow;
        minutes = minutes + 1;
        
        /* Maj variables */
        minutes_etape = minutes_etape + 1;
        minutes_all = minutes_all + 1;
    }
    //if one minute has passed, start counting milliseconds from zero again and add one minute to the clock.
    if (minutes >= 60)
    {
        minutes = 0;
        hours = hours + 1;
    }

    /*Serial.print("Time : ");
    Serial.print(hours );
    Serial.print(":");
    Serial.print(minutes );
    Serial.print(":");
    Serial.println(seconds );*/
}


/****************************************
* BLYNK
*/

/* SYNC CONNECT */
bool isFirstConnect = true;

BLYNK_CONNECTED()
{
    if (isFirstConnect)
    {
        Blynk.syncAll();
        Serial.print("BLYNK SYNC");
        isFirstConnect = false;
    }
}

void BlynkUpdate(void)
{
    /* Blynk run */
    Blynk.run();

    if ((timeNow - timeBlynkLast) >= BLYNK_UPDATE_SEC)
    {
        Serial.println("Update Blynk");
        Blynk.virtualWrite(DATA_OUT_TEMP, Temp_val);
        Blynk.virtualWrite(DATA_OUT_CONSIGNE, Temp_consigne);
        Blynk.virtualWrite(DATA_OUT_TEMP_GRAPH, Temp_val);
        Blynk.virtualWrite(DATA_OUT_STAGE, eEtatCourant);
        Blynk.virtualWrite(DATA_OUT_RELAY, Relay_val);
        Blynk.virtualWrite(DATA_MINUTES_ALL, minutes_all);
        Blynk.virtualWrite(DATA_MINUTES_ETAT, minutes_etape);
    
        lcd.clear();
        lcd.print(0, 0, LCD1); // use: (position X: 0-15, position Y: 0-1, "Message you want to print")
        lcd.print(0, 1, LCD2); // use: (position X: 0-15, position Y: 0-1, "Message you want to print")
    
        /* Relay Status */
        if (HIGH == Relay_val)
        {
            ledRelay.on();
        }
        else
        {
            ledRelay.off();
        }
    
        /* Maj time blynk sync*/
        timeBlynkLast =  timeNow ;
    }
}


/************************************************
 * BOUTON START/STOP
 */
BLYNK_WRITE(DATE_IN_START_STOP)
{
    StartStop = param.asInt(); 
    Serial.print("StartStop :");
    Serial.println(StartStop );
}

/************************************************
 * PARAMETRES
 */

/* Empatage */
BLYNK_WRITE(DATE_IN_C_EMPATAGE_CHAUFFE)
{
    EmpatageChauffeTemp = param.asInt(); 
    Serial.print("Empatage Temp Chauffe :");
    Serial.println(EmpatageChauffeTemp);
}

BLYNK_WRITE(DATE_IN_C_EMPATAGE_MAINTIEN)
{
    EmpatageMaintienTemp = param.asInt(); 
    Serial.print("Empatage Temp Maintien :");
    Serial.println(EmpatageMaintienTemp);
}

BLYNK_WRITE(DATE_IN_C_EMPATAGE_TEMPS)
{
    EmpatageMaintienDuree_min = param.asInt(); 
    Serial.print("Empatage Durée Maintien :");
    Serial.println(EmpatageMaintienDuree_min);
}

/* Chauffe */
BLYNK_WRITE(DATE_IN_C_CHAUFFE)
{
    ChauffeTemp = param.asInt(); 
    Serial.print("Chauffe Temp :");
    Serial.println(ChauffeTemp );
}

BLYNK_WRITE(DATE_IN_C_CHAUFFE_TEMPS)
{
    ChauffeDuree_min = param.asInt(); 
    Serial.print("Chauffe Durée :");
    Serial.println(ChauffeDuree_min );
}
