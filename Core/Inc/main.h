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
#define DQ3_Pin GPIO_PIN_8
#define DQ3_GPIO_Port GPIOA
#define DQ2_Pin GPIO_PIN_9
#define DQ2_GPIO_Port GPIOA
#define DQ5_Pin GPIO_PIN_8
#define DQ5_GPIO_Port GPIOC
#define DQ4_Pin GPIO_PIN_9
#define DQ4_GPIO_Port GPIOC
#define DQ1_Pin GPIO_PIN_10
#define DQ1_GPIO_Port GPIOC
#define DQ0_Pin GPIO_PIN_11
#define DQ0_GPIO_Port GPIOC
#define DQ8_Pin GPIO_PIN_8
#define DQ8_GPIO_Port GPIOD
#define DQ7_Pin GPIO_PIN_9
#define DQ7_GPIO_Port GPIOD
#define DQ6_Pin GPIO_PIN_10
#define DQ6_GPIO_Port GPIOD
#define ETHRST_Pin GPIO_PIN_11
#define ETHRST_GPIO_Port GPIOD
#define STAT_LED_Pin GPIO_PIN_9
#define STAT_LED_GPIO_Port GPIOE
#define FACT_RES_Pin GPIO_PIN_10
#define FACT_RES_GPIO_Port GPIOE
#define DQ11_Pin GPIO_PIN_12
#define DQ11_GPIO_Port GPIOE
#define DQ10_Pin GPIO_PIN_13
#define DQ10_GPIO_Port GPIOE
#define DQ9_Pin GPIO_PIN_14
#define DQ9_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
