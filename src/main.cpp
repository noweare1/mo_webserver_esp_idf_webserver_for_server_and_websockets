// ---------------------------------------------------------------------------------------
//
// Code for a webserver on the ESP32 to control LEDs (device used for tests: ESP32-WROOM-32D).
// The code allows user to switch between three LEDs and set the intensity of the LED selected
//
// For installation, the following libraries need to be installed:
// jrm : Using webServer from esp-idf framework which also includes websockets
// * ArduinoJson Library by Benoit Blanchon
//
// Written by mo thunderz (last update: 19.11.2021)
// Modified noweare1 6/5/2024
//
// ---------------------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_http_server.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Esp.h>

// #include <esp32-hal-log.h>  set up in platformio.ini file

static esp_err_t stop_webserver(httpd_handle_t);
static esp_err_t socket_handler(httpd_req_t *);
static esp_err_t root_get_handler(httpd_req_t *);
uint8_t *readFileUsingSize(fs::FS &fs, const char *path, size_t fsize);

// arduino_event_id_t event;
// typedef void (*WiFiEventCb)(arduino_event_id_t event);  //signature for wifi event handler
// wifi_event_id_t onEvent(wifi_event_handler, ARDUINO_EVENT_MAX);

uint8_t *buf = NULL; // holds the index.html file
const char *l_type;  // save & print key and value
int l_value;

const char *ssid = "boulderhill";
const char *password = "wideflower594";
static httpd_handle_t server = NULL;

// global variables of the LED selected and the intensity of that LED
int LED_selected = 0;
int LED_intensity = 50;

// init PINs: assign any pin on ESP32
int led_pin_0 = 4;
int led_pin_1 = 0;
int led_pin_2 = 2;

// ESP32 does not have "analogwrite" and uses ledcWrite instead
const int freq = 5000;
const int led_channels[] = {0, 1, 2};
const int resolution = 8;

static const httpd_uri_t ws = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = socket_handler,
    .user_ctx = NULL,
    .is_websocket = true};

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
    .is_websocket = false};

// Note: If log level is set to debug, log_d() messages in the library will be printed out.
// Use build_flags = -DCORE_DEBUG_LEVEL=ARDUHAL_LOG_LEVEL_INFO to just print info level logs
void wifi_event_handler(arduino_event_id_t event)
{
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_READY:
    log_i("wifi started %d", ARDUINO_EVENT_WIFI_READY);
    break;

  case ARDUINO_EVENT_WIFI_STA_START:
    log_i("wifi started %d", ARDUINO_EVENT_WIFI_STA_START);
    break;

  case ARDUINO_EVENT_WIFI_STA_STOP:
    log_i("wifi stopped %d", ARDUINO_EVENT_WIFI_STA_STOP);
    break;

  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    log_i("wifi connected %d", ARDUINO_EVENT_WIFI_STA_CONNECTED);
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    log_i("wifi disconnectd %d", ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    break;

  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    log_i("wifi got ip address %d", ARDUINO_EVENT_WIFI_STA_GOT_IP);
    break;

  default:
    log_i("Unknown WIFI Event %d", event);
    break;
  } // switch
} // wifi event handler

/*    STOP WEBSERVER  */
static esp_err_t stop_webserver(httpd_handle_t server)
{
  return httpd_stop(server); // Stop the httpd server
}

//--------determine file size -----------//
size_t getFileLength(fs::FS &fs, const char *path)
{
  size_t fileLength = 0;
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    Serial.println("- failed to open file for reading");
    return 0;
  }

  Serial.println("- calculating file size");
  while (file.available()) // use this if you do not know the file size
  {
    file.read();
    fileLength += 1;
  }

  file.close();
  return fileLength;
}

// -------- use this function if you know number of bytes of the file  --------//
uint8_t *readFileUsingSize(fs::FS &fs, const char *path, size_t fsize)
{
  uint8_t *file_buf = (uint8_t *)malloc((sizeof(uint8_t) * (fsize + 1))); // malloc buf size of file

  if (file_buf == NULL)
  {
    Serial.println("error mallocing space");
    return 0;
  }

  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) // if not a file or directory return
  {
    Serial.println("- failed to open file for reading");
    free(file_buf);
    return 0;
  }

  Serial.println("Reading from file:");

  /* DOCUMENTATION
  size_t File::read(uint8_t* buf, size_t size)
  {
    if (!*this) {
          return -1;
    }
     return _p->read(buf, size);
  }
  */
  file.read(file_buf, fsize); // reads file in a buffer of size fsize

  for (int i = 0; i < fsize; i++)
  {
    // look for null char
    if (file_buf[i] == '\0') // this makes the end of a file
      break;
  }

  file.close();
  return file_buf;
}

// HTTP GET Handler, sends index.html to client
static esp_err_t root_get_handler(httpd_req_t *req)
{
  uint8_t *buffer;
  // const uint32_t root_len = root_end - root_start;
  log_i("Serve root");

  size_t fileLength = getFileLength(SPIFFS, "/index.html");
  log_i("index.html is %ld bytes", fileLength);
  buffer = readFileUsingSize(SPIFFS, "/index.html", fileLength);

  httpd_resp_set_status(req, "202 OK"); // all this works
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, (char *)buffer, fileLength);
  // httpd_resp_sendstr(req, buff;
  free(buffer);

  /*
  uint32_t heapSize = ESP.getFreeHeap();  //check heap size for memory leak
  Serial.print("heap size : ");
  Serial.println(heapSize);
  */
  return ESP_OK;
}

/**********    SOCKET HANDLER  ********/
static esp_err_t socket_handler(httpd_req_t *req)
{
  uint8_t buf[65] = {0}; // make buf large enough to handle known requests

  if (req->method == HTTP_GET)
  {
    log_i("Handshake done, the new connection was opened");
    return ESP_OK;
  } // if

  // get socketId of this client (socketID is global)
  socketID = httpd_req_to_sockfd(req);

  /** Get request data  if any **/
  httpd_ws_frame_t ws_pkt;
  // uint8_t *buf = NULL; // uint8_t* payload is a member of httpd_ws_frame_t (ws_pkt)

  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t)); // init members of ws_pkt to 0
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;             // type will be text or may json ?

  /* GET LEN OF REQUEST PAYLOAD */
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0); // setting max_length parameter to 0
  if (ret != ESP_OK)                                    // stores framelength in ws_pkt.len
  {
    log_e("httpd_ws_recv_frame failed to get frame len with %d", ret);
    return ret;
  } // if
  log_i("frame len is %d", ws_pkt.len);

  /**    if packet length > 0  there is data     **/
  if (ws_pkt.len)
  {
    ws_pkt.payload = buf; // ws_pkt.payload gets assigned same address as buf so now it has access to allocated memory

    /** This call places request data in packet payload  **/
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK)
    {
      log_e("httpd_ws_recv_frame failed with %d", ret);
      // free(buf);
      return ret;
    } // if

    /**     Print out request payload data which is a json string    **/
    log_i("Got packet with message: %s", ws_pkt.payload);

    StaticJsonDocument<200> doc;                                       // create JSON container
    DeserializationError error = deserializeJson(doc, ws_pkt.payload); // here we construct a json object so we can extract the data
    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str()); // flash string or c_str (c string)
      ret = -1;
      free(buf); // free memory we allocated previously
      return ret;
    } // if
    else
    {
      l_type = doc["type"]; // this is how we extract type,value from json object
      l_value = doc["value"];

      if (String(l_type) == "LED_selected")
      {
        LED_selected = int(l_value);
        sprintf((char *)buf, "{\"type\":\"LED_selected\",\"value\":\"%d\"}", LED_selected);
        ws_pkt.len = strlen((const char *)buf);

        // send packet to client
        esp_err_t ret = httpd_ws_send_frame(req, &ws_pkt);

        for (int i = 0; i < 3; i++)
        {
          if (i == LED_selected)
            ledcWrite(led_channels[i], map(LED_intensity, 0, 100, 0, 255)); // switch on selectedLED
          else
            ledcWrite(led_channels[i], 0); // switch off not-selected LEDs
        } // for
      } // if

      // compare l_type to known keys
      if (String(l_type) == "LED_intensity")
      {
        LED_intensity = int(l_value);
        sprintf((char *)buf, "{\"type\":\"LED_intensity\",\"value\":\"%d\"}", LED_intensity); // much easier than messing with json
        ws_pkt.len = strlen((const char *)buf);

        esp_err_t ret = httpd_ws_send_frame(req, &ws_pkt);                         // send data to the client{"type":"LED_intensity","value":"84"}
        ledcWrite(led_channels[LED_selected], map(LED_intensity, 0, 100, 0, 255)); // updata outputs on esp32
      } // if

    } // else
    // free(buf); // make sure we do not return above without freeing buf
  } // if ws pkt len

  ret = ESP_OK;
  return ret;
}

/***  START WEBSERVER    ***/
static httpd_handle_t start_webserver(void)
{
  server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  log_i("Starting server on port: '%d'", config.server_port); // Start the httpd server
  if (httpd_start(&server, &config) == ESP_OK)
  {
    log_i("Registering URI handlers"); // Registering the ws handler

    // Documentation: esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler);
    httpd_register_uri_handler(server, &ws);
    httpd_register_uri_handler(server, &root);
    return server;
  }

  log_i("Error starting server!");
  return NULL;
}

/***          SETUP          ***/
void setup()
{

  Serial.begin(115200); // init serial port for debugging

  // bool SPIFFSFS::begin(bool formatOnFail, const char * basePath, uint8_t maxOpenFiles, const char * partitionLabel)
  if (!SPIFFS.begin(false, "/data", 5, "spiffs"))
  {
    Serial.println("SPIFFS Mount Failed !"); // git places yellow arrow to indicate modified line
    return;
  }

  WiFiEvent_t event;
  // WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP); // WiFiGotIP is the handler
  WiFi.onEvent(wifi_event_handler, event = ARDUINO_EVENT_MAX); // all wifi events will trigger the callback

  ledcSetup(led_channels[0], freq, resolution);
  ledcSetup(led_channels[1], freq, resolution);
  ledcSetup(led_channels[2], freq, resolution);

  // attach the channels to the led_pins to be controlled
  ledcAttachPin(led_pin_0, led_channels[0]);
  ledcAttachPin(led_pin_1, led_channels[1]);
  ledcAttachPin(led_pin_2, led_channels[2]);

  WiFi.begin(ssid, password);
  Serial.println("Establishing connection to WiFi with SSID: " + String(ssid)); // print SSID to the serial interface for debugging

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
  }
  Serial.print("Connected to network with IP address: ");
  Serial.println(WiFi.localIP()); // show IP address that the ESP32 has received from router

  // ledcWrite(led_channels[LED_selected], map(LED_intensity, 0, 100, 0, 255));

  server = start_webserver();
}

void loop()
{
}
