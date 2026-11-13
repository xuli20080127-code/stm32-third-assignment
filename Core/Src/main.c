/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "OLED.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct __attribute__((packed))
{
  uint8_t header;     // 固定帧头 AA
  uint8_t run;        // 00暂停，01运行
  uint8_t dir;        // 00正向，01反向
  uint16_t step_ms;   // 先低字节、后高字节的切换间隔
  uint8_t checksum;   // 前5字节相加后保留低8位
} RxPacket;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE BEGIN PD */
#define LED_ON  GPIO_PIN_RESET
#define LED_OFF GPIO_PIN_SET
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint8_t key_run_event = 0U;  // PA11 中断设置的运行/暂停事件
volatile uint8_t key_dir_event = 0U;  // PA12 中断设置的方向切换事件

uint8_t lamp_running = 1U;      // 0：暂停；1：运行
uint8_t lamp_direction = 0U;    // 0：正向；1：反向
uint16_t lamp_step_ms = 500U;  // 当前切换间隔，串口可修改
uint8_t lamp_led = 0U;  // 当前LED编号：0、1、2
uint8_t oled_dirty = 0U;  // 0：显示已同步；1：需要刷新
uint32_t last_run_key_tick = 0U;  // PA11 上一次有效按下的时间
uint32_t last_dir_key_tick = 0U;  // PA12 上一次有效按下的时间
uint8_t run_key_checking = 0U;    // PA11 正在等待 20ms 电平确认
uint8_t dir_key_checking = 0U;    // PA12 正在等待 20ms 电平确认
volatile uint8_t rx_error_flag = 0U;  // 整包接收重启失败标志
RxPacket rx_packet;                    // 串口中断写入的完整数据包
volatile uint8_t packet_ready = 0U;    // 1表示6字节完整包已收到

char feedback_buffer[64];              // 中断发送期间保持有效的文本
uint8_t feedback_pending = 0U;         // 1表示需要发送状态反馈
volatile uint8_t uart_tx_busy = 0U;   // 1表示USART1正在中断发送
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE BEGIN 0 */
static void OLED_RefreshStatus(void)
{
  OLED_ShowString(
      1, 1,
      (lamp_running == 0U) ? "STATE: pausing" : "STATE: running  "
  );

  OLED_ShowString(
      2, 1,
      (lamp_direction == 0U) ? "DIR: forward " : "DIR: backward"
  );
  uint32_t a =HAL_GetTick();
  OLED_ShowString(3, 1, "STEP:");
  OLED_ShowNum(3, 6, (uint32_t)lamp_step_ms, 4);

  OLED_ShowString(4, 1, "LED:");
  OLED_ShowNum(4, 5, (uint32_t)(lamp_led + 1U), 1);
}
/* USER CODE END 0 */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  OLED_Init();
  OLED_RefreshStatus();
  uint32_t last_tick = HAL_GetTick();
  HAL_StatusTypeDef rx_result;

  rx_result = HAL_UART_Receive_IT(
    &huart1,
    (uint8_t *)&rx_packet,
    sizeof(rx_packet)
);

  if (rx_result != HAL_OK)
  {
    Error_Handler();
  }
  uint8_t hello[] = "HELLO\r\n";  // 启动时发送的测试文本

  if (HAL_UART_Transmit(
          &huart1,
          hello,
          sizeof(hello) - 1,
          100) != HAL_OK)
  {
    Error_Handler();  // 串口发送失败时进入错误处理
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();  // 获取本轮主循环的当前时间
    if (packet_ready != 0U)
    {
      RxPacket packet = rx_packet;  // 复制本地副本
      packet_ready = 0U;            // 当前包已经签收

      const uint8_t *bytes = (const uint8_t *)&packet;

      uint8_t expected =
          (uint8_t)(bytes[0] + bytes[1] + bytes[2]
                  + bytes[3] + bytes[4]);

      // 所有检查同时通过才更新状态
      if (packet.header == 0xAAU &&
          packet.checksum == expected &&
          packet.run <= 1U &&
          packet.dir <= 1U &&
          packet.step_ms >= 100U &&
          packet.step_ms <= 2000U)
      {
        lamp_running = packet.run;
        lamp_direction = packet.dir;
        lamp_step_ms = packet.step_ms;
        oled_dirty = 1U;
        feedback_pending = 1U;
      }

      // 当前包处理完后，重新启动下一包接收
      if (HAL_UART_Receive_IT(
              &huart1,
              (uint8_t *)&rx_packet,
              sizeof(rx_packet)) != HAL_OK)
      {
        rx_error_flag = 1U;
      }
    }

    // 如果重启接收失败，主循环继续重试
    if (rx_error_flag != 0U)
    {
      if (HAL_UART_Receive_IT(
              &huart1,
              (uint8_t *)&rx_packet,
              sizeof(rx_packet)) == HAL_OK)
      {
        rx_error_flag = 0U;
      }
    }

    if (key_run_event != 0U)  // 收到 PA11 下降沿事件
    {
      key_run_event = 0U;
      last_run_key_tick = now;
      run_key_checking = 1U;
    }

    if (run_key_checking != 0U &&
        (now - last_run_key_tick) >= 20U)  // 等待触点稳定 20ms
    {
      run_key_checking = 0U;

      if (HAL_GPIO_ReadPin(KEY_RUN_GPIO_Port, KEY_RUN_Pin)
          == GPIO_PIN_RESET)  // 仍为低电平，确认按键按下
      {
        lamp_running = (lamp_running == 0U) ? 1U : 0U;
        oled_dirty = 1U;
        feedback_pending = 1U;
      }
    }

    if (key_dir_event != 0U)  // 收到 PA12 下降沿事件
    {
      key_dir_event = 0U;
      last_dir_key_tick = now;
      dir_key_checking = 1U;
    }

    if (dir_key_checking != 0U &&
        (now - last_dir_key_tick) >= 20U)  // 等待触点稳定 20ms
    {
      dir_key_checking = 0U;

      if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12)
          == GPIO_PIN_RESET)  // 仍为低电平，确认按键按下
      {
        lamp_direction = (lamp_direction == 0U) ? 1U : 0U;
        oled_dirty = 1U;
        feedback_pending = 1U;
      }
    }

    if (lamp_running == 0U)         // 暂停状态，保持当前 LED
    {
      last_tick = now;            // 暂停时重新计时
    }
    else if ((now - last_tick) >= lamp_step_ms)  // 运行且已满 500ms
    {
      last_tick = now;            // 记录本次切换时间
      oled_dirty = 1U;
      if (lamp_direction == 0U)  // 正向：0 -> 1 -> 2 -> 0
      {
        lamp_led++;

        if (lamp_led >= 3U)
        {
          lamp_led = 0U;
        }
      }
      else  // 反向：2 -> 1 -> 0 -> 2
      {
        if (lamp_led == 0U)
        {
          lamp_led = 2U;
        }
        else
        {
          lamp_led--;
        }
      }
    }
    HAL_GPIO_WritePin(
    LED0_GPIO_Port,
    LED0_Pin,
    (lamp_led == 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET
);

    HAL_GPIO_WritePin(
        LED1_GPIO_Port,
        LED1_Pin,
        (lamp_led == 1U) ? GPIO_PIN_RESET : GPIO_PIN_SET
    );

    HAL_GPIO_WritePin(
        LED2_GPIO_Port,
        LED2_Pin,
        (lamp_led == 2U) ? GPIO_PIN_RESET : GPIO_PIN_SET
    );
    if (oled_dirty != 0U)
    {
      oled_dirty = 0U;
      OLED_RefreshStatus();
    }
    if (feedback_pending != 0U && uart_tx_busy == 0U)
    {
      int message_length = snprintf(
          feedback_buffer,
          sizeof(feedback_buffer),
          "STATE=%s,DIR=%s,STEP=%u,LED=%u\r\n",
          (lamp_running == 0U) ? "pausing" : "running",
          (lamp_direction == 0U) ? "forward" : "backward",
          (unsigned int)lamp_step_ms,
          (unsigned int)(lamp_led + 1U)
      );

      if (message_length > 0 &&
          message_length < (int)sizeof(feedback_buffer))
      {
        uart_tx_busy = 1U;

        if (HAL_UART_Transmit_IT(
                &huart1,
                (uint8_t *)feedback_buffer,
                (uint16_t)message_length) == HAL_OK)
        {
          feedback_pending = 0U;
        }
        else
        {
          uart_tx_busy = 0U;
        }
      }
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == KEY_RUN_Pin)
  {
    key_run_event = 1U;
  }

  if (GPIO_Pin == GPIO_PIN_12)
  {
    key_dir_event = 1U;
  }
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    packet_ready = 1U;  // 完整6字节已收进rx_packet
  }
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    uart_tx_busy = 0U;  // 本条反馈已经发送完成
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
