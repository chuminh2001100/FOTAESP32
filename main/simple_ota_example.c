/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "freertos/event_groups.h"
#include "string.h"
#include "mqtt.h"
#include "esp_wifi.h"
#include "protocol_examples_common.h"

#include "nvs.h"
#include "nvs_flash.h"

#define EXAMPLE_ESP_WIFI_SSID      "IOT_3"
#define EXAMPLE_ESP_WIFI_PASS      "0987654321"
#define EXAMPLE_ESP_MAXIMUM_RETRY  10   
#define BUTTON_PIN GPIO_NUM_0  // Example button pin, change to match your setup
#define OTA_URL_SIZE 256
#define authorization "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY3RpdmF0ZSI6ZmFsc2UsImFkbWluSWQiOiI5Mzg5OTdkMy0zM2ZjLTRhNTMtYTgxMi0xMTJiZGNjMjk0ZDMiLCJhdXRob3JpemVkIjp0cnVlLCJjbXBSb2xlIjoiIiwiZXhwIjoxNzE3MDY1MjY5LCJvcmdJZCI6IiIsInBlcm1pc3Npb25zIjoicHJvamVjdF9pZCwqLHJvbGVfaWQsVEVOQU5UIiwicGhvbmUiOiIiLCJwcm9qZWN0SWQiOiIiLCJyb2xlSWQiOiIiLCJyb2xlVHlwZSI6IiIsInN5c3RlbVJvbGUiOiJURU5BTlQiLCJ1c2VySWQiOiI5Mzg5OTdkMy0zM2ZjLTRhNTMtYTgxMi0xMTJiZGNjMjk0ZDMiLCJ1c2VyTmFtZSI6ImhvbmdjdGExQHZpZXR0ZWwuY29tLnZuIn0.t50ZIzSHGVOX425PVxb0i2JFPfboiolLmXVxK0zPZVg"

#define mqtt_broker "mqtt://mqttvcloud.innoway.vn:1883"
#define host        "http://apivcloud.innoway.vn"
#define domain      "mqtt://172.21.5.244:1883"

static const char *TAG = "OTA_ME";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");
#define TIME2_BUTTON_BIT      BIT0
#define TIME4_BUTTON_BIT      BIT1
#define MQTT_CONNECT_BIT      BIT2
#define MQTT_DISCONNECT_BIT   BIT3
#define WIFI_FAIL_BIT         BIT0
#define WIFI_CONNECTED_BIT    BIT1
#define LED_PIN 2

static int s_retry_num = 0;
char status_topic[] = "v1/devices/me/telemetry";
char ota_res_topic[] = "messages/598fbd77-2b4b-4937-8f8c-2a6e75aad99e/fota_response";
char ota_topic[] = "messages/598fbd77-2b4b-4937-8f8c-2a6e75aad99e/ota";
char attributes_topic[] = "messages/598fbd77-2b4b-4937-8f8c-2a6e75aad99e/attributets";

static EventGroupHandle_t mqtt_event_group;
static EventGroupHandle_t s_wifi_event_group;
static EventGroupHandle_t button_event_group;
esp_mqtt_client_handle_t mqtt_client;

// static const int CONNECTED_MQTT = BIT0;
// static const int DISCONNECT_MQTT = BIT1;
TaskHandle_t MQTT_TASK;//Led_TASK dung de dinh chi hoac tiep tuc chay task led_task (task 1)
TaskHandle_t HTTP_TASK;
TaskHandle_t BUTTON_HANDLE_TASK;
char url_ota[200] = {0};
char tmp[300];
char version_firm[50] = {0};
char res_ota[100] = {0};
bool old_button_state = false;
bool button_state = true;
int led_state_1 = 0;
gpio_config_t io_conf;
bool connect_mqtt = false;
/*--------------------------Process mqtt_OTA----------------------------------------*/
int filter_comma(char *respond_data, int begin, int end, char *output, char exChar)
{
	memset(output, 0, strlen(output));
	int count_filter = 0, lim = 0, start = 0, finish = 0,i;
	for (i = 0; i < strlen(respond_data); i++)
	{
		if ( respond_data[i] == exChar)
		{
			count_filter ++;
			if (count_filter == begin)			start = i+1;
			if (count_filter == end)			finish = i;
		}

	}
	if(count_filter < end)
	{
	    finish = strlen(respond_data);
	}
	lim = finish - start;
	for (i = 0; i < lim; i++){
		output[i] = respond_data[start];
		start ++;
	}
	output[i] = 0;
	return 0;
}

esp_err_t my_http_client_init_callback(esp_http_client_handle_t client) {
    // Implement custom initialization logic here
    printf("HTTP client initialized\n");
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", authorization);
    return 0; // Return ESP_OK
}

esp_err_t esp_https_ota_2(const esp_http_client_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "esp_http_client config not found");
        return ESP_ERR_INVALID_ARG;
    }

    esp_https_ota_config_t ota_config = {
        .http_config = config,
        .http_client_init_cb = my_http_client_init_callback,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (https_ota_handle == NULL) {
        return ESP_FAIL;
    }

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
    }

    if (err != ESP_OK) {
        esp_https_ota_abort(https_ota_handle);
        return err;
    }

    esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
    if (ota_finish_err != ESP_OK) {
        return ota_finish_err;
    }
    return ESP_OK;
}

bool process_ota(char *respond_data, char *url_data, char *version){
	char *a = NULL;
    char *b = NULL;
    printf("khong duoc cay: %s",respond_data);
	memset(url_data,0,strlen(url_data));
    memset(version,0,strlen(version));
	// memcpy(url_data,"http://apivcloud.innoway.vn",strlen("http://apivcloud.innoway.vn"));
	char tmp2[200] = {0};
    char tmp3[100] = {0};
    b = strstr(respond_data,"\"version");
    if(b != NULL){
        printf("Gia tri cua b: %s",b);
        filter_comma(b,3,4,tmp3,'\"');
        strcat(version,tmp3);
        printf("\nVersion: \r\n%s",version);
    }
	a = strstr(respond_data,"\"url_api");
    if(a != NULL){
        printf("Gia tri cua a: %s",a);
        filter_comma(a,3,4,tmp2,'\"');
        printf("DATA: \r\n%s",tmp2);
        strcat(url_data,tmp2);
        printf("\nURL: \r\n%s",url_data);
        return true;
    }
    return false;
}

/*---------------------------------------------------------------------------------*/
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void gpio_ouput_init(gpio_config_t *io_conf, uint8_t gpio_num){
    io_conf->mode = GPIO_MODE_OUTPUT;
    io_conf->pin_bit_mask = 1 << gpio_num;
    io_conf->pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf->pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf->intr_type = GPIO_INTR_DISABLE;
    gpio_config(io_conf);
}


// void wifi_init_sta(void)
// {
//     tcpip_adapter_init();
//     s_wifi_event_group = xEventGroupCreate();

//     ESP_ERROR_CHECK(esp_netif_init());

//     ESP_ERROR_CHECK(esp_event_loop_create_default());
//     esp_netif_create_default_wifi_sta();

//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));

//     ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
//                                                         ESP_EVENT_ANY_ID,
//                                                         &event_handler,
//                                                         NULL,
//                                                         NULL));
//     ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
//                                                         IP_EVENT_STA_GOT_IP,
//                                                         &event_handler,
//                                                         NULL,
//                                                         NULL));

//     wifi_config_t wifi_config = {
//         .sta = {
//             .ssid = EXAMPLE_ESP_WIFI_SSID,
//             .password = EXAMPLE_ESP_WIFI_PASS,
//             /* Setting a password implies station will connect to all security modes including WEP/WPA.
//              * However these modes are deprecated and not advisable to be used. Incase your Access point
//              * doesn't support WPA2, these mode can be enabled by commenting below line */
// 	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
//         },
//     };
//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
//     ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
//     ESP_ERROR_CHECK(esp_wifi_start() );

//     ESP_LOGI(TAG, "wifi_init_sta finished.");

//     /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
//      * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
//     EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
//             WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
//             pdFALSE,
//             pdFALSE,
//             portMAX_DELAY);

//     /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
//      * happened. */
//     if (bits & WIFI_CONNECTED_BIT) {
//         ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
//                  EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
//     } else if (bits & WIFI_FAIL_BIT) {
//         ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
//                  EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
//     } else {
//         ESP_LOGE(TAG, "UNEXPECTED EVENT");
//     }
// }

void button_task(void* parameter) {
    int button_press_count = 0;
    TickType_t last_button_press_time = 0;

    while (1) {
        int button_state = gpio_get_level(BUTTON_PIN);

        if (button_state != old_button_state && button_state == 0) {
            TickType_t current_time = xTaskGetTickCount();

            if (current_time - last_button_press_time > pdMS_TO_TICKS(1000)) {
                // More than 1 second has passed since the last button press
                button_press_count = 1;
            } else {
                button_press_count++;
                if (button_press_count == 2) {
                    printf("2 times\n");
                    led_state_1 = !led_state_1;
                    xEventGroupSetBits(button_event_group, TIME2_BUTTON_BIT);
                } 
                else if (button_press_count == 4) {
                    xEventGroupSetBits(button_event_group, TIME4_BUTTON_BIT);
                    printf("4 times\n");
                    button_press_count = 0;
                }
            }

            last_button_press_time = current_time;
        }
        old_button_state = button_state;

        vTaskDelay(100 / portTICK_PERIOD_MS);  // Adjust the delay as needed
    }
}


void init_mqtt(){
    mqtt_event_group = xEventGroupCreate();
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;
}

void simple_ota_example_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting OTA example");

    esp_http_client_config_t config = {
        .url = url_ota,
        .cert_pem = (char *)server_cert_pem_start,
        .event_handler = _http_event_handler,
        .keep_alive_enable = true,
        .method = HTTP_METHOD_POST,
    };

#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL_FROM_STDIN
    char url_buf[OTA_URL_SIZE];
    if (strcmp(config.url, "FROM_STDIN") == 0) {
        example_configure_stdin_stdout();
        fgets(url_buf, OTA_URL_SIZE, stdin);
        int len = strlen(url_buf);
        url_buf[len - 1] = '\0';
        config.url = url_buf;
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong firmware upgrade image url");
        abort();
    }
#endif
    config.skip_cert_common_name_check = true;
    esp_err_t ret = esp_https_ota_2(&config);
    if (ret == ESP_OK) {
        sprintf(res_ota,"{\"version\": \"%s\",\r\n\"status\":\"Success\"\n}",version_firm);
        esp_mqtt_client_publish(mqtt_client, ota_res_topic, res_ota, 0, 0, 1);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        esp_restart();
    } else 
    {
        sprintf(res_ota,"{\"version\": \"%s\",\r\n\"status\":\"Failed\"\n}",version_firm);
        esp_mqtt_client_publish(mqtt_client, ota_res_topic, res_ota, 0, 0, 1);
        ESP_LOGE(TAG, "Firmware upgrade failed");
    }
    while (1) {
        printf("Task in OTA\r\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{   
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    int length = 0;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            vTaskResume(MQTT_TASK);
            xEventGroupSetBits(button_event_group, MQTT_CONNECT_BIT);
            esp_mqtt_client_subscribe(client, "messages/598fbd77-2b4b-4937-8f8c-2a6e75aad99e/ota", 0);
            esp_mqtt_client_subscribe(client, "messages/598fbd77-2b4b-4937-8f8c-2a6e75aad99e/fota_request", 0);
            // esp_mqtt_client_subscribe(client, "v2/devices/me/telemetry", 0);
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            break;
        case MQTT_EVENT_DISCONNECTED:
            vTaskSuspend(MQTT_TASK);
            xEventGroupClearBits(button_event_group, MQTT_CONNECT_BIT);
            //khi mat ket noi mqtt thi tien hanh dinh chi 2 task nay de tranh lam treo chuong trinh
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            printf("TOPIC=%.*s\r\n",event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len,event->data);
            length = event->data_len;
            printf("Length of content: %d\r\n",length);
            memcpy(tmp,event->data,length);
            tmp[event->data_len] = '\0';
            printf("Data in event: %.*s",strlen(tmp),tmp);
            if(process_ota(tmp,url_ota,version_firm) == true){
                vTaskSuspend(MQTT_TASK);
                xTaskCreate(&simple_ota_example_task, "ota_example_task", 8192, NULL, 3, HTTP_TASK);
            }
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}




void mqtt_task(void *pvParameter){
    char tmp[100];
    while(1){
        sprintf(tmp,"{\"temp\": %d,\"humidity\": \"%d\"}", 36, 60);
        int msg_id = esp_mqtt_client_publish(mqtt_client, attributes_topic, tmp, 0, 0, 1);
        ESP_LOGI(TAG, "Sent publish successful, msg_id=%d", msg_id);
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
}

void handle_button_task(void *pvParameter){
    while(1){
        xEventGroupWaitBits(button_event_group,TIME4_BUTTON_BIT, true, false, portMAX_DELAY);
            // kiem tra co ket noi toi internet chua, sau do thi moi publish du lieu
        EventBits_t bits = xEventGroupGetBits(button_event_group);
        if (bits & MQTT_CONNECT_BIT) {
            gpio_set_level(LED_PIN, led_state_1);  // Set LED state based on the received value
            char message[100];
            int led_pin = 2;
            sprintf(message, "{\"led\": %d,\"status\": \"%d\"}", led_pin, led_state_1);
            int msg_id = esp_mqtt_client_publish(mqtt_client, ota_topic, message, 0, 0, 1);
            ESP_LOGI(TAG,"Sent ota of button 4 times\r\n");
            ESP_LOGI(TAG, "Sent publish successful, msg_id=%d", msg_id);
            ESP_LOGI(TAG,"Data:%d",led_state_1);
        }
    }
}



void app_main(void)
{
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    button_event_group = xEventGroupCreate();
    gpio_ouput_init(&io_conf,LED_PIN);
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);

    ESP_ERROR_CHECK(example_connect());
    // wifi_init_sta();
    // printf("begin connect\r\n");
    // vTaskSuspend(HTTP_TASK);
    // printf("finish connect\r\n");
    // xTaskCreate(&simple_ota_example_task, "ota_example_task", 8192, NULL, 3, HTTP_TASK);
    // vTaskSuspend(HTTP_TASK);
    esp_mqtt_client_config_t mqtt_config = {
    .uri = mqtt_broker,
    .username = "abc2",
    .password = "tIx9StGRwj8b6YUAIdLtDi7ODgCNAXeH",
    .event_handle = mqtt_event_handler,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_start(mqtt_client);
    xTaskCreate(&mqtt_task, "mqtt_test_task", 2048, NULL, 3, &MQTT_TASK);
    xTaskCreate(&handle_button_task, "hand_test_task", 2048, NULL, 3, &BUTTON_HANDLE_TASK);
    vTaskSuspend(MQTT_TASK);
    while(1)
    {
        // sprintf(tmp,"{\"led\": %d,\"status\": \"%d\"}", 2, 8);
        printf("API TEST\r\n");
        // int msg_id = esp_mqtt_client_publish(mqtt_client, status_topic, tmp, 0, 0, 1);
        // ESP_LOGI(TAG, "Sent publish successful, msg_id=%d", msg_id);
        vTaskDelay(200);
    }
}

