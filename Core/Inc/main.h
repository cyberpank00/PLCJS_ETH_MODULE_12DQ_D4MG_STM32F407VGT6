/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ETHINT_Pin GPIO_PIN_1
#define ETHINT_GPIO_Port GPIOB
#define ETHRST_Pin GPIO_PIN_11
#define ETHRST_GPIO_Port GPIOD
#define STAT_LED_Pin GPIO_PIN_8
#define STAT_LED_GPIO_Port GPIOC
#define FACT_RES_Pin GPIO_PIN_6
#define FACT_RES_GPIO_Port GPIOC
#define DI11_Pin GPIO_PIN_10
#define DI11_GPIO_Port GPIOC
#define DI10_Pin GPIO_PIN_11
#define DI10_GPIO_Port GPIOC
#define DI9_Pin GPIO_PIN_12
#define DI9_GPIO_Port GPIOC
#define DI8_Pin GPIO_PIN_0
#define DI8_GPIO_Port GPIOD
#define DI7_Pin GPIO_PIN_1
#define DI7_GPIO_Port GPIOD
#define DI6_Pin GPIO_PIN_2
#define DI6_GPIO_Port GPIOD
#define DI5_Pin GPIO_PIN_3
#define DI5_GPIO_Port GPIOD
#define DI4_Pin GPIO_PIN_4
#define DI4_GPIO_Port GPIOD
#define DI3_Pin GPIO_PIN_5
#define DI3_GPIO_Port GPIOD
#define DI2_Pin GPIO_PIN_6
#define DI2_GPIO_Port GPIOD
#define DI1_Pin GPIO_PIN_7
#define DI1_GPIO_Port GPIOD
#define DI0_Pin GPIO_PIN_3
#define DI0_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
