#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp32-hal-ledc.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "index_html.h"
#include "secret.h"

#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "fd_forward.h"
#include <ArduinoJson.h>
#include <UniversalTelegramBot.h>
#include <string.h>

bool global_isSendPhoto = false;

WiFiClientSecure clientTCP;
UniversalTelegramBot bot(BOT_TOKEN, clientTCP);

#define FLASH_LED_PIN 4
bool flashState = LOW;

String global_Feedback = "";
//Checks for new messages every 1 second.
int global_botRequestDelay = 1000;
unsigned long global_lastTimeBotRan;

String Command = "";
String cmd = "";
String P1 = "";
String P2 = "";
String P3 = "";
String P4 = "";
String P5 = "";
String P6 = "";
String P7 = "";
String P8 = "";
String P9 = "";

byte ReceiveState = 0;
byte cmdState = 1;
byte strState = 1;
byte questionstate = 0;
byte equalstate = 0;
byte semicolonstate = 0;

typedef struct {
  size_t size;
  size_t index;
  size_t count;
  int sum;
  int *values;
} ra_filter_t;

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

//truyền hình ảnh qua giao thức HTTP từ ESP32-CAM
#define BOUNDARY_PART "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" BOUNDARY_PART;
static const char *_STREAM_BOUNDARY = "\r\n--" BOUNDARY_PART "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static ra_filter_t ra_filter;
httpd_handle_t stream_httpd = NULL;
httpd_handle_t global_camera_httpd = NULL;

//--Khởi tạo filter->values-sample=(int *)20
void *ra_filter_init(ra_filter_t *filter, size_t sample_size) {
  filter->values = (int *)calloc(sample_size * sizeof(int));
  if (!filter->values) {
    return;
  }
  filter->size = sample_size;
  return;
}

//Hàm này được sử dụng để tính toán giá trị trung bình động của một chuỗi các giá trị đầu vào, giúp làm mượt dữ liệu và giảm nhiễu trong các ứng dụng xử lý tín hiệu hoặc dữ liệu.s
int ra_filter_run(ra_filter_t *filter, int value) {
  if (!filter->values) {
    return value;
  }
  filter->sum -= filter->values[filter->index];
  filter->values[filter->index] = value;
  filter->sum += filter->values[filter->index];
  filter->index++;
  filter->index = filter->index % filter->size;
  if (filter->count < filter->size) {
    filter->count++;
  }
  return filter->sum / filter->count;
}

void setupCamera() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sscb_sda = 26;
  config.pin_sscb_scl = 27;
  config.pin_pwdn = 32;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;

  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 10;  //0-63 lower number means higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;  //0-63 lower number means higher quality
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }
}

void setup() {
  setupCamera();

  WiFi.mode(WIFI_AP_STA);  //--Khởi tạo "Access Point Station" cho ESP32-Cam.
  WiFi.begin(LOCAL_SSID, LOCAL_PASSWORD);
  clientTCP.setCACert(TELEGRAM_CERTIFICATE_ROOT);  // Thêm chứng chỉ gốc của Telegram
  Serial.println("\nConnecting to: " + String(LOCAL_SSID));

  long int StartTime = millis();
  while ((WiFi.status() != WL_CONNECTED) && ((millis() - StartTime) < 10000)) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.softAP((WiFi.localIP().toString() + "_" + String(AP_STA_SSID)).c_str(), String(AP_STA_PASSWORD));
    Serial.println("\nSTAIP address: " + WiFi.localIP().toString());
  } else {
    WiFi.softAP((WiFi.softAPIP().toString() + "_" + String(AP_STA_SSID)).c_str(), String(AP_STA_PASSWORD));
    Serial.println("Failed connect to wifi ... ");
  }

  Serial.println("\nAPIP address: " + WiFi.softAPIP().toString());
  startCameraServer();
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  //http://192.168.xxx.xxx/
  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
  };

  //http://192.168.xxx.xxx/status
  httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = NULL
  };

  //http://192.168.xxx.xxx/control
  httpd_uri_t cmd_uri = {
    .uri = "/control",
    .method = HTTP_GET,
    .handler = cmd_handler,
    .user_ctx = NULL
  };

  //http://192.168.xxx.xxx/capture
  httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
  };

  //http://192.168.xxx.xxx:81/stream
  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };

  ra_filter_init(&ra_filter, 20);

  Serial.printf("Starting web server on port: '%d'\n", config.server_port);  //Server Port
  if (httpd_start(&global_camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(global_camera_httpd, &index_uri);
    httpd_register_uri_handler(global_camera_httpd, &cmd_uri);
    httpd_register_uri_handler(global_camera_httpd, &status_uri);
    httpd_register_uri_handler(global_camera_httpd, &capture_uri);
  }

  config.server_port += 1;  //Stream Port
  config.ctrl_port += 1;    //UDP Port
  Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

void loop() {
  if (global_isSendPhoto) {
    Serial.println("Preparing photo");
    sendPhotoTelegram();
    global_isSendPhoto = false;
  }

  // delay 1s
  if (millis() > global_lastTimeBotRan + global_botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      Serial.println("Got response from Telegram");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    global_lastTimeBotRan = millis();
  }

  // Check mask condition and take appropriate action
  if (P1 == "Mask") {
    Serial.println("[!Update] Trạng thái: Có khẩu trang");
    sendPhotoTelegram();
    String notifyMessage = "Đeo khẩu trang an toàn \n";
    bot.sendMessage(CHAT_ID, notifyMessage, "");
    global_isSendPhoto = false;
  } else if (P1 == "No%20mask") {
    sendPhotoTelegram();
    bot.sendMessage(CHAT_ID, "Không đeo khẩu trang \n", "");
    global_isSendPhoto = false;
  } else if (P1 == "No%20one%20here") {
    sendPhotoTelegram();
    bot.sendMessage(CHAT_ID, "Không có người \n", "");
    global_isSendPhoto = false;
  }
}

// Dùng trong capture_handler
static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) {
    j->len = 0;
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
    return 0;
  }
  j->len += len;
  return len;
}

//--Fetch .html Website UI
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

//--Xử lý ClientRequest để lấy ảnh (mỗi request - 1 ảnh) từ ESP32-Cam
static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  int64_t fr_start = esp_timer_get_time();

  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  size_t out_len, out_width, out_height;
  uint8_t *out_buf;
  bool s;
  if (fb->width > 400) {
    size_t fb_len = 0;
    if (fb->format == PIXFORMAT_JPEG) {
      fb_len = fb->len;
      res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    } else {
      jpg_chunking_t jchunk = { req, 0 };
      res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
      httpd_resp_send_chunk(req, NULL, 0);
      fb_len = jchunk.len;
    }
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    Serial.printf("JPG: %uB %ums\n", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));
    return res;
  }

  dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
  if (!image_matrix) {
    esp_camera_fb_return(fb);
    Serial.println("dl_matrix3du_alloc failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  out_buf = image_matrix->item;
  out_len = fb->width * fb->height * 3;
  out_width = fb->width;
  out_height = fb->height;

  s = fmt2rgb888(fb->buf, fb->len, fb->format, out_buf);
  esp_camera_fb_return(fb);
  if (!s) {
    dl_matrix3du_free(image_matrix);
    Serial.println("to rgb888 failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  jpg_chunking_t jchunk = { req, 0 };
  s = fmt2jpg_cb(out_buf, out_len, out_width, out_height, PIXFORMAT_RGB888, 90, jpg_encode_stream, &jchunk);
  dl_matrix3du_free(image_matrix);
  if (!s) {
    Serial.println("JPEG compression failed");
    return ESP_FAIL;
  }

  int64_t fr_end = esp_timer_get_time();
  return res;
}

//--Xử lý ClientStreamRequest để liên tục lấy dữ liệu qua luồng stream từ ESP32-Cam
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[64];
  dl_matrix3du_t *image_matrix = NULL;
  int64_t fr_start = 0;
  int64_t fr_ready = 0;

  static int64_t last_frame = 0;
  if (!last_frame) {
    last_frame = esp_timer_get_time();
  }

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    //--Lấy ra con trỏ trỏ đến khung hình ESP32-Cam
    fb = esp_camera_fb_get();
    
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      fr_start = esp_timer_get_time();
      fr_ready = fr_start;
      if (fb->width > 400) {
        if (fb->format != PIXFORMAT_JPEG) {
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if (!jpeg_converted) {
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      } else {
        image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);

        if (!image_matrix) {
          Serial.println("dl_matrix3du_alloc failed");
          res = ESP_FAIL;
        } else {
          if (!fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item)) {
            Serial.println("fmt2rgb888 failed");
            res = ESP_FAIL;
          } else {
            fr_ready = esp_timer_get_time();
            if (fb->format != PIXFORMAT_JPEG) {
              if (!fmt2jpg(image_matrix->item, fb->width * fb->height * 3, fb->width, fb->height, PIXFORMAT_RGB888, 90, &_jpg_buf, &_jpg_buf_len)) {
                Serial.println("fmt2jpg failed");
                res = ESP_FAIL;
              }
              esp_camera_fb_return(fb);
              fb = NULL;
            } else {
              _jpg_buf = fb->buf;
              _jpg_buf_len = fb->len;
            }
          }
          dl_matrix3du_free(image_matrix);
        }
      }
    }

    //--Bắt đầu respond thông tin cho Client trong luồng stream.
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }

    //--Dọn dẹp bộ nhớ ESP32-Cam
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }

    if (res != ESP_OK) {
      break;
    }
    //--Theo dõi và tính toán tốc độ khung hình (FPS) của video
    int64_t fr_end = esp_timer_get_time();
    int64_t ready_time = (fr_ready - fr_start) / 1000;
    int64_t frame_time = fr_end - last_frame;
    last_frame = fr_end;
    frame_time /= 1000;
    uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
    Serial.printf("MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps), %u+%u+%u+%u=%u %s%d\n",
                  (uint32_t)(_jpg_buf_len),
                  (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time,
                  avg_frame_time, 1000.0 / avg_frame_time);
  }

  last_frame = 0;
  return res;
}

//--Xử lý ClientRequest: {var: val} hoặc Complicated_Command(myCmd)
static esp_err_t cmd_handler(httpd_req_t *req) {
  char *resultBufferStorage;
  size_t reqLength;
  char variable[128] = {
    0,
  };
  char value[128] = {
    0,
  };
  String myCmd = "";

  // Lấy thông tin từ URL
  // http://example.com/camera?var=brightness&val=50
  reqLength = httpd_req_get_url_query_len(req) + 1;
  if (reqLength > 1) {
    resultBufferStorage = (char *)malloc(reqLength);

    if (!resultBufferStorage) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    if (httpd_req_get_url_query_str(req, resultBufferStorage, reqLength) == ESP_OK) {
      if (httpd_query_key_value(resultBufferStorage, "var", variable, sizeof(variable)) == ESP_OK
          && httpd_query_key_value(resultBufferStorage, "val", value, sizeof(value)) == ESP_OK) {
        int val = atoi(value);
        sensor_t *s = esp_camera_sensor_get();
        int res = 0;

        if (strcmp(variable, "framesize") == 0) {
          if (s->pixformat == PIXFORMAT_JPEG)
            res = s->set_framesize(s, (framesize_t)val);
        }
        else if (strcmp(variable, "quality") == 0) res = s->set_quality(s, val);
        else if (strcmp(variable, "contrast") == 0) res = s->set_contrast(s, val);
        else if (strcmp(variable, "brightness") == 0) res = s->set_brightness(s, val);
        else if (strcmp(variable, "hmirror") == 0) res = s->set_hmirror(s, val);
        else if (strcmp(variable, "vflip") == 0) res = s->set_vflip(s, val);
        else if (strcmp(variable, "flash") == 0) {
          ledcAttachPin(4, 4);
          ledcSetup(4, 5000, 8);
          ledcWrite(4, val);
        }
        else res = -1;

        if (res != 0) return httpd_resp_send_500(req);

        if (resultBufferStorage) {
          global_Feedback = String(resultBufferStorage);
          const char *resp = global_Feedback.c_str();
          httpd_resp_set_type(req, "text/html");
          httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
          return httpd_resp_send(req, resp, strlen(resp));
        } else {
          httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
          return httpd_resp_send(req, NULL, 0);
        }
      } else {  //--Request thành công nhưng không có (var; val) path-variables.
        myCmd = String(resultBufferStorage);
        global_Feedback = "";
        Command = "";
        cmd = "";
        P1 = "", P2 = "", P3 = "", P4 = "", P5 = "", P6 = "", P7 = "", P8 = "", P9 = "";
        ReceiveState = 0, cmdState = 1, strState = 1, questionstate = 0, equalstate = 0, semicolonstate = 0;

        //--Xử lý myCmd
        if (myCmd.length() > 0) {
          myCmd = "?" + myCmd + ";";
          //?serial=No%20mask;0.9971154928207397;stop;
          Serial.println("myCmd: " + myCmd);

          int currentInd = 0;
          if (cmd.indexOf("?") != -1) {
            cmd = myCmd.substring(currentInd + 1, myCmd.indexOf("="));
            currentInd = myCmd.indexOf("=") + 1;  //--Bỏ dấu "="

            P1 = myCmd.substring(currentInd, myCmd.indexOf(";"));
            currentInd = myCmd.indexOf(";") + 1;  //--Bỏ dấu ";"

            int currentSemicolon = currentInd - 1;
            for (int i = 2; i <= 9; i++) {
              currentSemicolon = myCmd.indexOf(";", currentSemicolon);
              switch (i) {
                case 2: P2 = myCmd.substring(currentInd, currentSemicolon); break;
                case 3: P3 = myCmd.substring(currentInd, currentSemicolon); break;
                case 4: P4 = myCmd.substring(currentInd, currentSemicolon); break;
                case 5: P5 = myCmd.substring(currentInd, currentSemicolon); break;
                case 6: P6 = myCmd.substring(currentInd, currentSemicolon); break;
                case 7: P7 = myCmd.substring(currentInd, currentSemicolon); break;
                case 8: P8 = myCmd.substring(currentInd, currentSemicolon); break;
                case 9: P9 = myCmd.substring(currentInd, currentSemicolon); break;
              }
              currentInd = currentSemicolon + 1;  //--Bỏ dấu ";"
              currentSemicolon = myCmd.indexOf(";", currentInd);

              if (currentInd == myCmd.length() || currentSemicolon == -1)
                break;
            }
          }
        }

        if (cmd.length() > 0) {
          Serial.println("\ncmd= " + cmd + " ,P1= " + P1 + " ,P2= " + P2 + " ,P3= " + P3 + " ,P4= " + P4 + " ,P5= " + P5 + " ,P6= " + P6 + " ,P7= " + P7 + " ,P8= " + P8 + " ,P9= " + P9 + "\n");

          if (cmd == "ip") {
            global_Feedback = "AP IP: " + WiFi.softAPIP().toString();
            global_Feedback += "<br>";
            global_Feedback += "STA IP: " + WiFi.localIP().toString();
          }
          else if (cmd == "mac") global_Feedback = "STA MAC: " + WiFi.macAddress();
          else if (cmd == "restart") ESP.restart();
          else if (cmd == "digitalwrite") {
            ledcDetachPin(P1.toInt());  //--Đặt mặc định
            pinMode(P1.toInt(), OUTPUT);//--Thay đổi giá trị chân GPIO theo P1
            digitalWrite(P1.toInt(), P2.toInt());
          }
          else if (cmd == "digitalread") global_Feedback = String(digitalRead(P1.toInt()));
          else if (cmd == "analogwrite") {
            if (P1 == "4") {
              ledcAttachPin(4, 4);
              ledcSetup(4, 5000, 8);
              ledcWrite(4, P2.toInt());
            } else {
              ledcAttachPin(P1.toInt(), 9);
              ledcSetup(9, 5000, 8);
              ledcWrite(9, P2.toInt());
            }
          } else if (cmd == "analogread") global_Feedback = String(analogRead(P1.toInt()));
          else if (cmd == "touchread") global_Feedback = String(touchRead(P1.toInt()));
          else if (cmd == "flash") {
            ledcAttachPin(4, 4);
            ledcSetup(4, 5000, 8);
            int val = P1.toInt();
            ledcWrite(4, val);
          }
          //cmd= serial ,P1= No%20mask ,P2= 0.8156958818435669 ,P3= stop ,P4=  ,P5=  ,P6=  ,P7=  ,P8=  ,P9=
          else if (cmd == "serial") {
            if (P1 != "" & P1 != "stop") Serial.println(P1);
            if (P2 != "" & P2 != "stop") Serial.println(P2);
            Serial.println();
            //No%20mask
            //0.9977319240570068
          } else global_Feedback = "Command is not defined";

          if (global_Feedback == "")
            global_Feedback = Command;

          const char *resp = global_Feedback.c_str();
          httpd_resp_set_type(req, "text/html");
          httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
          return httpd_resp_send(req, resp, strlen(resp));
        }
      }
    }
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
}

//--Fetch Initialized Data cho website giao diện hiển thị (brightness, quality,...)
static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];

  sensor_t *s = esp_camera_sensor_get();
  char *p = json_response;
  *p++ = '{';
  p += sprintf(p, "\"flash\":%d,", 0);
  p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p += sprintf(p, "\"quality\":%u,", s->status.quality);
  p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
  p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
  p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
  p += sprintf(p, "\"vflip\":%u", s->status.vflip);
  *p++ = '}';
  *p++ = 0;

  // output: {"flash":0,"framesize":5,"quality":10,"brightness":0,"contrast":0,"hmirror":0,"vflip":0}

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

/* ============ TELEGRAM SENDING MESSAGE ============ */
void handleNewMessages(int numNewMessages) {
  Serial.print("Handle New Messages: ");
  Serial.println(numNewMessages);

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

    // Print the received message
    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;
    if (text == "/start") {
      String welcome = "Welcome , " + from_name + "\n";
      welcome += "Use the following commands to interact with the ESP32-CAM \n";
      welcome += "/photo : takes a new photo\n";
      welcome += "/flash : toggles flash LED \n";
      bot.sendMessage(CHAT_ID, welcome, "");
    }
    if (text == "/flash") {
      flashState = !flashState;
      digitalWrite(FLASH_LED_PIN, flashState);
      Serial.println("Change flash LED state");
    }
    if (text == "/photo") {
      global_isSendPhoto = true;
      Serial.println("New photo request");
    }
  }
}

String sendPhotoTelegram() {
  String getAll = "";
  String getBody = "";

  //Dispose first picture because of bad quality
  camera_fb_t *fb = NULL;
  fb = esp_camera_fb_get();
  esp_camera_fb_return(fb);  // dispose the buffered image

  // Take a new photo
  fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
    return "Camera capture failed";
  }

  Serial.println("Connect to " + String(TELEGRAM_DOMAIN));

  if (clientTCP.connect(TELEGRAM_DOMAIN, 443)) {
    Serial.println("Connection successful");

    String head = "--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + CHAT_ID
                  + "\r\n--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--RandomNerdTutorials--\r\n";

    size_t imageLen = fb->len;
    size_t extraLen = head.length() + tail.length();
    size_t totalLen = imageLen + extraLen;

    clientTCP.println("POST /bot" + BOT_TOKEN + "/global_isSendPhoto HTTP/1.1");
    clientTCP.println("Host: " + String(TELEGRAM_DOMAIN));
    clientTCP.println("Content-Length: " + String(totalLen));
    clientTCP.println("Content-Type: multipart/form-data; boundary=RandomNerdTutorials");
    clientTCP.println();
    clientTCP.print(head);

    //Chia dữ liệu ảnh thành các khối 1024 byte và gửi từng khối một.
    //Nếu còn lại ít hơn 1024 byte, gửi phần còn lại.
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n = 0; n < fbLen; n = n + 1024) {
      if (n + 1024 < fbLen) {
        clientTCP.write(fbBuf, 1024);
        fbBuf += 1024;
      } else if (fbLen % 1024 > 0) {
        size_t remainder = fbLen % 1024;
        clientTCP.write(fbBuf, remainder);
      }
    }

    clientTCP.print(tail);
    // Giải phóng bộ nhớ ảnh
    esp_camera_fb_return(fb);

    int waitTime = 10000;  // timeout 10 seconds
    long startTimer = millis();
    boolean state = false;

    //--Chờ phản hồi từ server và đọc dữ liệu để theo dõi trên Logger (Serial Monitor)
    while ((startTimer + waitTime) > millis()) {
      Serial.print(".");
      delay(100);
      while (clientTCP.available()) {
        char c = clientTCP.read();
        if (state == true) getBody += String(c);
        if (c == '\n') {
          if (getAll.length() == 0) state = true;
          getAll = "";
        } else if (c != '\r')
          getAll += String(c);
        startTimer = millis();
      }
      if (getBody.length() > 0) break;
    }
    clientTCP.stop();
    Serial.println(getBody);
  } else {
    getBody = "Connected to api.telegram.org failed.";
    Serial.println("Connected to api.telegram.org failed. ");
  }
  return getBody;
}
/* ========================================================== */