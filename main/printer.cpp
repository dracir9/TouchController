/**
 * @file   printer.cpp
 * @author Ricard Bitriá Ribes (https://github.com/dracir9)
 * Created Date: 28-04-2022
 * -----
 * Last Modified: 10-03-2023
 * Modified By: Ricard Bitriá Ribes
 * -----
 * @copyright (c) 2022 Ricard Bitriá Ribes
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "printer.h"
#include "dbg_log.h"
#include <cstring>
#include <freertos/FreeRTOS.h>

bool Printer::_init = false;
Printer Printer::_instance;

Printer::Printer()
{
    // Initialize comunication
    uart_config_t uart_config = {};
        uart_config.baud_rate = 115200;
        uart_config.data_bits = UART_DATA_8_BITS;
        uart_config.parity = UART_PARITY_DISABLE;
        uart_config.stop_bits = UART_STOP_BITS_1;
        uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uart_config.rx_flow_ctrl_thresh = 122;

    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uartNum, &uart_config));

    ESP_ERROR_CHECK(uart_set_pin(uartNum, 17, 18, -1, -1));

    // Setup UART buffered IO with event queue
    ESP_ERROR_CHECK(uart_driver_install(uartNum, uartBufferSize, uartBufferSize, RxQueueLen, &uartQueue, 0));

    // Flags
    readyFlag = xSemaphoreCreateBinary();
    if (readyFlag == nullptr)
    {
        DBG_EARLY_LOGE("Failed to create card detect semaphore");
        esp_restart();
    }

    // Queues
    uartTxBuffer = xRingbufferCreate(TxBuffLen, RINGBUF_TYPE_NOSPLIT);
    if (uartTxBuffer == nullptr)
    {
        DBG_EARLY_LOGE("Failed to create queues!");
        esp_restart();
    }

    // Create tasks
    xTaskCreate(serialRxTask, "UART RX task", 4096, this, 1, &uartRxTask);
    if (uartRxTask == nullptr)
    {
        DBG_EARLY_LOGE("Failed to create serial receiver task");
        esp_restart();
    }

    _init = true;
}

Printer::~Printer()
{
}

Printer* Printer::instance()
{
    return _init ? &_instance : nullptr;
}

void Printer::serialRxTask(void* arg)
{
    DBG_LOGI("Starting UART receiver task");
    Printer* CNC = static_cast<Printer*>(arg);
    uart_event_t event;
    size_t linePtr = 0;
    char* dtmp = (char*) malloc(CNC->maxLineLen);
    char* line = (char*) malloc(CNC->maxLineLen);
    if (dtmp == nullptr || line == nullptr)
    {
        DBG_LOGE("Unable to allocate buffers");
        esp_restart();
    }

    size_t len = 0;
    while (CNC)
    {
        if(xQueueReceive(CNC->uartQueue, (void *)&event, portMAX_DELAY) != pdTRUE)
            continue;

        switch(event.type)
        {
        // Data received
        case UART_DATA:
        {
            while ((len = uart_read_bytes(CNC->uartNum, dtmp, CNC->maxLineLen, 0)) > 0)
            {
                for (size_t i = 0; i < len; i++)
                {
                    if (dtmp[i] < 32)
                    {
                        if (linePtr == 0) continue;
                        line[linePtr] = '\0';
                        CNC->parseSerial(line, linePtr);
                        linePtr = 0;
                    }
                    else
                        line[linePtr++] = dtmp[i];
                        
                    event.size--;
                }
            }

            if (xSemaphoreTake(CNC->readyFlag, 0) == pdTRUE)
            {
                TxPacket *packet = nullptr;
                while ((packet = (TxPacket *)xRingbufferReceive(CNC->uartTxBuffer, &len, 0)) != nullptr)
                {
                    if (packet->id == UART_SEND_CMD)
                    {
                        uart_write_bytes(CNC->uartNum, &packet->data, packet->len);
                    }
                    else if (packet->id == UART_SEND_LINE)
                    {
                        CNC->sendLine();
                    }
                        
                    vRingbufferReturnItem(CNC->uartTxBuffer, packet);
                }
            }
            break;
        }
        //Event of HW FIFO overflow detected
        case UART_FIFO_OVF:
            DBG_LOGW("HW fifo overflow");
            uart_flush_input(CNC->uartNum);
            xQueueReset(CNC->uartQueue);
            break;
        //Event of UART ring buffer full
        case UART_BUFFER_FULL:
            DBG_LOGW("Ring buffer full");
            uart_flush_input(CNC->uartNum);
            xQueueReset(CNC->uartQueue);
            break;
        //Event of UART RX break detected
        case UART_BREAK:
            DBG_LOGI("uart rx break");
            break;
        //Event of UART parity check error
        case UART_PARITY_ERR:
            DBG_LOGI("uart parity error");
            break;
        //Event of UART frame error
        case UART_FRAME_ERR:
            DBG_LOGI("uart frame error");
            break;
        //UART_PATTERN_DET
        case UART_PATTERN_DET: // Not used, should never be called
            DBG_LOGI("Pattern detected");
            break;
        //Others
        default:
            DBG_LOGI("uart event type: %d", event.type);
            break;
        }
    }

    vTaskDelete(nullptr);
}

void Printer::parseSerial(char* str, const size_t len)
{
    // Skip initial spaces
    while (*str == ' ') str++;

    if (strncmp(str, "ok", 2) == 0)
    {
        xSemaphoreGive(readyFlag);
    }
    else if (strncmp(str, "T:", 2) == 0)
    {
        if (parseTemp(str) != ESP_OK)
            printf(">|%s\n", str);
    }
    else if (strncmp(str, "X:", 2) == 0)
    {
        if(parsePos(str) != ESP_OK)
            printf(">|%s\n", str);
    }
    else if (strncmp(str, "echo:;", 6) == 0)
    {
        return;
    }
    else if (strncmp(str, "echo: ", 6) == 0)
    {
        if (strncmp(&str[6], " G2", 3) == 0 && str[10] == ' ')
        {
            if (str[9] == '1')
            {
                isMetric = true;
                DBG_LOGD("Normal units");
            }
            else if (str[9] == '0')
            {
                isMetric = false;
                DBG_LOGD("R******d units");
            }
        }
        else if (strncmp(&str[6], " M92 ", 5) == 0)
        {
            char* endChr = &str[11];
            while (*endChr != '\0')
            {
                if (*endChr == 'X')
                    stpsPerUnit.x = strtof(&endChr[1], &endChr);
                else if (*endChr == 'Y')
                    stpsPerUnit.y = strtof(&endChr[1], &endChr);
                else if (*endChr == 'Z')
                    stpsPerUnit.z = strtof(&endChr[1], &endChr);
                else if (*endChr == 'E')
                    endChr++; // TODO: Add extruder
                else
                    endChr++;
            }
        }
        else if (strncmp(&str[6], " M149 ", 6) == 0 && str[13] == ' ')
        {
            tempUnit = str[12];
            DBG_LOGD("Teperature unit: %c", tempUnit);
        }
        else if (strncmp(&str[6], " M200 ", 6) == 0)
        {
            char* endChr = &str[12];
            while (*endChr != '\0')
            {
                if (*endChr == 'D')
                    endChr++;
                else if (*endChr == 'L')
                    endChr++;
                else if (*endChr == 'S')
                {
                    if (endChr[1] == '1')
                        volumetricEn = true;
                    else if (endChr[1] == '0')
                        volumetricEn = false;
                    endChr++;
                }
                else if (*endChr == 'T')
                {
                    uint8_t num = (endChr[1] - '0') + 1;
                    toolheads = (num > toolheads && num < 10) ? num : toolheads;
                    DBG_LOGI("Tools: %d", toolheads);
                    endChr++;
                }
                else
                    endChr++;
            }
        }
        else if (strncmp(&str[6], " M301 ", 6) == 0)
        {
            if (state == READ_CNFG)
            {
                state = INIT;
                allocateFields();
            }
        }
        else
            printf(">|%s\n", str);
    }
    else if (strncmp(str, "Cap:", 4) == 0)
    {
        if (strncmp(&str[4], "AUTOREPORT_POS:1", 16) == 0)
        {
            setAutoReportPos(true);
        }
        else if (strncmp(&str[4], "AUTOREPORT_TEMP:1", 17) == 0)
        {
            setAutoReportTemp(true);
        }
        else
            printf(">|%s\n", str);
    }
    else if (strcmp(str, "start") == 0 || strncmp(str, "FIRMWARE_NAME:Marlin", 20) == 0)
    {
        if (state == OFFLINE || str[0] == 's')
            state = READ_CNFG;
        else
            return;
        
        DBG_LOGI("Printer initialized");

        if (str[0] == 'F')
            getSettings();
        else
        {
            getFwInfo();
            xSemaphoreGive(readyFlag);
        }
    }
    else if (strncmp(str, "Marlin ", 7) == 0)
    {
        fwVersion = str;
        DBG_LOGD("FW version captured");
    }
    else
        printf(">|%s\n", str);
}

esp_err_t Printer::sendFile(std::string path)
{
    if (state != READY)
        return ESP_ERR_INVALID_STATE;
    if (file != nullptr)
        return ESP_ERR_NOT_FINISHED;
        
    filePath = path;
    file = fopen(filePath.c_str(), "r");
    DBG_LOGE_AND_RETURN_IF(file == nullptr, ESP_FAIL,
        "Error opening file \"%s\"", filePath.c_str());

    state = PRINTING;

    sendTxEvent(UART_SEND_LINE);

    DBG_LOGI("File open");
    return ESP_OK;
}

void Printer::sendLine()
{
    static char line[96];
    if (file == nullptr) return;

    char* ret = nullptr;
    do {
        ret = fgets(line, 96, file);
    } while (line[0] == ';' && ret != nullptr);
    
    if (ret == nullptr)
    {
        // Print completed
        if (feof(file))
            DBG_LOGI("Print complete!");
        else if (ferror(file))
            DBG_LOGE("Error reading file \"%s\"", filePath.c_str());

        fclose(file);
        file = nullptr;
        state = READY;
    }
    else
    {
        // Send line
        DBG_LOGD("Send line");
        sendCommand(line, strlen(line));
        
        // Queue next line
        sendTxEvent(UART_SEND_LINE);
    }
}

esp_err_t Printer::parseTemp(char* str)
{
    if (state < READY) return ESP_OK;
    // T:174.67 /0.00 B:173.28 /0.00 T0:174.67 /0.00 T1:172.17 /0.00 @:0 B@:0 @0:0 @1:0
    char* endChr = &str[2];

    // Active tool
    currentTemp[activeTool] = strtof(&endChr[0], &endChr);
    while (*endChr == ' ') endChr++;
    if (endChr[0] != '/')
        return ESP_FAIL;

    targetTemp[activeTool] = strtof(&endChr[1], &endChr);

    // Bed
    while (*endChr == ' ') endChr++;
    if (strncmp(endChr, "B:", 2) == 0)
    {
        bedTemp = strtof(&endChr[2], &endChr);
        while (*endChr == ' ') endChr++;
        if (endChr[0] != '/')
            return ESP_FAIL;

        tarBedTemp = strtof(&endChr[1], &endChr);
    }

    // Tools
    if (toolheads < 2) return ESP_OK;

    for (uint8_t i = 0; i < toolheads; i++)
    {
        while (*endChr == ' ') endChr++;
        if (endChr[0] != 'T' || endChr[1] != i + '0')
            return ESP_FAIL;

        currentTemp[i] = strtof(&endChr[3], &endChr);
        while (*endChr == ' ') endChr++;
        if (endChr[0] != '/')
            return ESP_FAIL;

        targetTemp[i] = strtof(&endChr[1], &endChr);
    }

    return ESP_OK;
}

esp_err_t Printer::parsePos(char* str)
{
    if (state < READY) return ESP_OK;
    //X:0.00 Y:0.00 Z:0.00 E:0.00 Count X:0 Y:0 Z:0
    char* endChr = &str[2];

    pos.x = strtof(endChr, &endChr);

    while (*endChr == ' ') endChr++;
    if (strncmp(endChr, "Y:", 2) != 0)
        return ESP_FAIL;

    pos.y = strtof(&endChr[2], &endChr);

    while (*endChr == ' ') endChr++;
    if (strncmp(endChr, "Z:", 2) != 0)
        return ESP_FAIL;

    pos.z = strtof(&endChr[2], &endChr);

    while (*endChr == ' ') endChr++;
    if (strncmp(endChr, "E:", 2) != 0)
        return ESP_FAIL;

    pos_E[activeTool] = strtof(&endChr[2], &endChr);

    return ESP_OK;
}

void Printer::allocateFields()
{
    if (state != INIT) return;

    // Make sure all memory is freed
    cleanFields();

    // Temperatures
    currentTemp = (float*)calloc(toolheads, sizeof(float));
    targetTemp = (float*)calloc(toolheads, sizeof(float));
    if (currentTemp == nullptr || targetTemp == nullptr)
    {
        DBG_LOGE("No memory!");
    }

    // Position
    pos_E = (float*)calloc(toolheads, sizeof(float));
    if (pos_E == nullptr)
    {
        DBG_LOGE("No memory!");
    }

    // Offsets
    offset_E = (float*)calloc(toolheads, sizeof(float));
    if (offset_E == nullptr)
    {
        DBG_LOGE("No memory!");
    }

    // Steps per unit
    stpsPerUnit_E = (float*)calloc(toolheads, sizeof(float));
    if (stpsPerUnit_E == nullptr)
    {
        DBG_LOGE("No memory!");
    }

    // Max feedrate
    maxFeedrate_E = (float*)calloc(toolheads, sizeof(float));
    if (maxFeedrate_E == nullptr)
    {
        DBG_LOGE("No memory!");
    }

    // Max acceleration
    maxAccel_E = (float*)calloc(toolheads, sizeof(float));
    if (maxAccel_E == nullptr)
    {
        DBG_LOGE("No memory!");
    }

    // PID settings
    hotendPID = (Vec3f*)calloc(toolheads, sizeof(Vec3f));
    if (hotendPID == nullptr)
    {
        DBG_LOGE("No memory!");
    }

    state = READY;
}

void Printer::cleanFields()
{
    // Temperatures
    if (currentTemp != nullptr)
    {
        free(currentTemp);
        currentTemp = nullptr;
    }
    if (targetTemp != nullptr)
    {
        free(targetTemp);
        targetTemp = nullptr;
    }

    // Position
    if (pos_E != nullptr)
    {
        free(pos_E);
        pos_E = nullptr;
    }

    // Offsets
    if (offset_E != nullptr)
    {
        free(offset_E);
        offset_E = nullptr;
    }

    // Steps per unit
    if (stpsPerUnit_E != nullptr)
    {
        free(stpsPerUnit_E);
        stpsPerUnit_E = nullptr;
    }

    // Max feedrate
    if (maxFeedrate_E != nullptr)
    {
        free(maxFeedrate_E);
        maxFeedrate_E = nullptr;
    }

    // Max acceleration
    if (maxAccel_E != nullptr)
    {
        free(maxAccel_E);
        maxAccel_E = nullptr;
    }

    // PID settings
    if (hotendPID != nullptr)
    {
        free(hotendPID);
        hotendPID = nullptr;
    }
}

esp_err_t Printer::sendCommand(const char *cmd, size_t len)
{
    if (state >= READY)
    {
        TxPacket *packet = nullptr;
        // Acquire memory from ring buffer and send command
        if (xRingbufferSendAcquire(uartTxBuffer, (void**)&packet, sizeof(TxPacket) + len, portMAX_DELAY) != pdTRUE)
            return ESP_FAIL;

        packet->id = UART_SEND_CMD;
        memcpy(packet->data, cmd, len);
        packet->len = len;

        DBG_LOGE_IF(xRingbufferSendComplete(uartTxBuffer, packet) != pdTRUE, "Error sending TX event");

        // Unlock UART queue
        uart_event_t evt = {};
        evt.type = UART_DATA;
        if (xQueueSend(uartQueue, &evt, portMAX_DELAY) != pdTRUE)
            return ESP_FAIL;
        
        return ESP_OK;
    }
    else
        return ESP_ERR_INVALID_STATE;
}

esp_err_t Printer::sendTxEvent(TxEvent event)
{
    if (state >= READY)
    {
        TxPacket *packet = nullptr;
        // Acquire memoty from ring buffer and send command
        if (xRingbufferSendAcquire(uartTxBuffer, (void**)&packet, sizeof(TxPacket), portMAX_DELAY) != pdTRUE)
            return ESP_FAIL;

        packet->id = UART_SEND_LINE;
        packet->len = 0;

        DBG_LOGE_IF(xRingbufferSendComplete(uartTxBuffer, packet) != pdTRUE, "Error sending TX event");

        // Unlock UART queue
        uart_event_t evt = {};
        evt.type = UART_DATA;
        if (xQueueSend(uartQueue, &evt, portMAX_DELAY) != pdTRUE)
            return ESP_FAIL;
        
        return ESP_OK;
    }
    else
        return ESP_ERR_INVALID_STATE;
}

esp_err_t Printer::getSettings()
{
    return sendCommand("M503\n", 5);
}

esp_err_t Printer::getFwInfo()
{
    return sendCommand("M115\n", 5);
}

uint8_t Printer::getToolNum() const
{
    if (state >= READY)
        return toolheads;
    else
        return 0;
}

PState Printer::getState() const
{
    return state;
}

float Printer::getBedTemp() const
{
    if (state >= READY)
        return bedTemp;
    else
        return 0.0f;
}

float Printer::getTarBedTemp() const
{
    if (state >= READY)
        return tarBedTemp;
    else
        return 0.0f;
}

float Printer::getToolTemp(uint8_t tool) const
{
    if (tool >= toolheads) tool = toolheads - 1;

    if (state >= READY)
        return currentTemp[tool];
    else
        return 0.0f;
}

float Printer::getTarToolTemp(uint8_t tool) const
{
    if (tool >= toolheads) tool = toolheads - 1;

    if (state >= READY)
        return targetTemp[tool];
    else
        return 0.0f;
}

esp_err_t Printer::getPosition(Vec3f* vec) const
{
    if (state >= READY)
    {
        *vec = pos;
        return ESP_OK;
    }
    else
    {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t Printer::getExtruderPos(float* pos, uint8_t tool) const
{
    if (state >= READY)
    {
        if (tool >= toolheads) tool = toolheads - 1;
        *pos = pos_E[tool];
        return ESP_OK;
    }
    else
    {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t Printer::setAutoReportPos(bool enable)
{
    if (enable)
        return sendCommand("M154 S1\n", 8);
    else
        return sendCommand("M154 S0\n", 8);
}

esp_err_t Printer::setAutoReportTemp(bool enable)
{
    if (enable)
        return sendCommand("M155 S1\n", 8);
    else
        return sendCommand("M155 S0\n", 8);
}
