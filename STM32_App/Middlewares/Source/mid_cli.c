/*
 * FreeRTOS+CLI V1.0.4 (C) 2014 Real Time Engineers ltd.  All rights reserved.
 *
 * This file is part of the FreeRTOS+CLI distribution.  The FreeRTOS+CLI license
 * terms are different to the FreeRTOS license terms.
 *
 * FreeRTOS+CLI uses a dual license model that allows the software to be used
 * under a standard GPL open source license, or a commercial license.  The
 * standard GPL license (unlike the modified GPL license under which FreeRTOS
 * itself is distributed) requires that all software statically linked with
 * FreeRTOS+CLI is also distributed under the same GPL V2 license terms.
 * Details of both license options follow:
 *
 * - Open source licensing -
 * FreeRTOS+CLI is a free download and may be used, modified, evaluated and
 * distributed without charge provided the user adheres to version two of the
 * GNU General Public License (GPL) and does not remove the copyright notice or
 * this text.  The GPL V2 text is available on the gnu.org web site, and on the
 * following URL: http://www.FreeRTOS.org/gpl-2.0.txt.
 *
 * - Commercial licensing -
 * Businesses and individuals that for commercial or other reasons cannot comply
 * with the terms of the GPL V2 license must obtain a low cost commercial
 * license before incorporating FreeRTOS+CLI into proprietary software for
 * distribution in any form.  Commercial licenses can be purchased from
 * http://shop.freertos.org/cli and do not require any source files to be
 * changed.
 *
 * FreeRTOS+CLI is distributed in the hope that it will be useful.  You cannot
 * use FreeRTOS+CLI unless you agree that you use the software 'as is'.
 * FreeRTOS+CLI is provided WITHOUT ANY WARRANTY; without even the implied
 * warranties of NON-INFRINGEMENT, MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. Real Time Engineers Ltd. disclaims all conditions and terms, be they
 * implied, expressed, or statutory.
 *
 * 1 tab == 4 spaces!
 *
 * http://www.FreeRTOS.org
 * http://www.FreeRTOS.org/FreeRTOS-Plus
 *
 */

/* Standard includes. */
#include "mid_CLI.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

UART_HandleTypeDef cmuPort;

/* Utils includes. */

typedef struct xCOMMAND_INPUT_LIST
{
    const CLI_Command_Definition_t *pxCommandLineDefinition;
    struct xCOMMAND_INPUT_LIST *pxNext;
} CLI_Definition_List_Item_t;

/*
 * The callback function that is executed when "help" is entered.  This is the
 * only default command that is always present.
 */
static BaseType_t prvHelpCommand (char *pcWriteBuffer, size_t xWriteBufferLen,const char *pcCommandString);

/*
 * Return the number of parameters that follow the command name.
 */
static int8_t prvGetNumberOfParameters (const char *pcCommandString);

/* The definition of the "help" command.  This command is always at the front
 of the list of registered commands. */
static const CLI_Command_Definition_t xHelpCommand =
{ 
    "help", 
    "\r\nhelp:\r\n Lists all the registered commands\r\n\r\n",
    prvHelpCommand, 
    0 
};

/* The definition of the list of commands.  Commands that are registered are
 added to this list. */
static CLI_Definition_List_Item_t xRegisteredCommands =
{ 
    &xHelpCommand, /* The first command in the list is always the help command, defined in this file. */
    NULL /* The next pointer is initialised to NULL, as there are no other registered commands yet. */
};

/* A buffer into which command outputs can be written is declared here, rather
 than in the command console implementation, to allow multiple command consoles
 to share the same buffer.  For example, an application may allow access to the
 command interpreter by UART and by Ethernet.  Sharing a buffer is done purely
 to save RAM.  Note, however, that the command console itself is not re-entrant,
 so only one command interpreter interface can be used at any one time.  For that
 reason, no attempt at providing mutual exclusion to the cOutputBuffer array is
 attempted. */
static char cOutputBuffer[configCOMMAND_INT_MAX_OUTPUT_SIZE];

/*
 * 判断当前的写BUF指针是否已经进入临界区，避免写入过多信息，导致越界
 * */
bool mid_CliWritePointCheck (char *pCurrent)
{
    return (abs(pCurrent - cOutputBuffer) > 50 ? true : false);
}
/*-----------------------------------------------------------*/

BaseType_t MidCLI_RegisterCommand (const CLI_Command_Definition_t * const pxCommandToRegister)
{
    static CLI_Definition_List_Item_t *pxLastCommandInList = &xRegisteredCommands;
    CLI_Definition_List_Item_t *pxNewListItem;
    BaseType_t xReturn = pdFAIL;

    /* Check the parameter is not NULL. */
    configASSERT(pxCommandToRegister);

    /* Create a new list item that will reference the command being registered. */
    pxNewListItem = (CLI_Definition_List_Item_t *) pvPortMalloc (sizeof(CLI_Definition_List_Item_t));
    configASSERT(pxNewListItem);

    if (pxNewListItem != NULL)
    {
        taskENTER_CRITICAL();
        {
            /* Reference the command being registered from the newly created
            list item. */
            pxNewListItem->pxCommandLineDefinition = pxCommandToRegister;

            /* The new list item will get added to the end of the list, so
            pxNext has nowhere to point. */
            pxNewListItem->pxNext = NULL;

            /* Add the newly created list item to the end of the already existing
            list. */
            pxLastCommandInList->pxNext = pxNewListItem;

            /* Set the end of list marker to the new list item. */
            pxLastCommandInList = pxNewListItem;
        }
        taskEXIT_CRITICAL();

        xReturn = pdPASS;
    }

    return xReturn;
}
/*-----------------------------------------------------------*/

BaseType_t MidCLI_ProcessCommand (const char * const pcCommandInput, 
                                    char * pcWriteBuffer,
                                    size_t xWriteBufferLen)
{
    static const CLI_Definition_List_Item_t *pxCommand = NULL;
    BaseType_t xReturn = pdTRUE;
    const char *pcRegisteredCommandString;
    size_t xCommandStringLength;

    /* Note:  This function is not re-entrant.  It must not be called from more
    thank one task. */
    if (pxCommand == NULL)
    {
        /* Search for the command string in the list of registered commands. */
        for (pxCommand = &xRegisteredCommands; pxCommand != NULL ; pxCommand = pxCommand->pxNext)
        {
            pcRegisteredCommandString = pxCommand->pxCommandLineDefinition->pcCommand;
            xCommandStringLength = strlen (pcRegisteredCommandString);

            /* To ensure the string lengths match exactly, so as not to pick up
            a sub-string of a longer command, check the byte after the expected
            end of the string is either the end of the string or a space before
            a parameter. */
            if ((pcCommandInput[xCommandStringLength] == ' ')
            || (pcCommandInput[xCommandStringLength] == 0x00))
            {
                if (strncmp (pcCommandInput, pcRegisteredCommandString,xCommandStringLength) == 0)
                {
                    /* The command has been found.  Check it has the expected
                    number of parameters.  If cExpectedNumberOfParameters is -1,
                    then there could be a variable number of parameters and no
                    check is made. */
                    if (pxCommand->pxCommandLineDefinition->cExpectedNumberOfParameters >= 0)
                    {
                        if (prvGetNumberOfParameters (pcCommandInput)
                        != pxCommand->pxCommandLineDefinition->cExpectedNumberOfParameters)
                        {
                            xReturn = pdFALSE;
                        }
                    }

                    break;
                }
            }
        }
    }

    if ((pxCommand != NULL ) && (xReturn == pdFALSE))
    {
        /* The command was found, but the number of parameters with the command
        was incorrect. */
        strncpy (
        pcWriteBuffer,
        "Incorrect command parameter(s).  Enter \"help\" to view a list of available commands.\r\n\r\n",
        xWriteBufferLen);
        pxCommand = NULL;
    }
    else if (pxCommand != NULL)
    {
        /* Call the callback function that is registered to this command. */
        xReturn = pxCommand->pxCommandLineDefinition->pxCommandInterpreter (
        pcWriteBuffer, xWriteBufferLen, pcCommandInput);

        /* If xReturn is pdFALSE, then no further strings will be returned
        after this one, and	pxCommand can be reset to NULL ready to search
        for the next entered command. */
        if (xReturn == pdFALSE)
        {
            pxCommand = NULL;
        }
    }
    else
    {
        /* pxCommand was NULL, the command was not found. */
        //strncpy( pcWriteBuffer, "Command not recognised.  Enter 'help' to view a list of available commands.\r\n\r\n", xWriteBufferLen );
        strncpy (
        pcWriteBuffer,
        "Command not recognised.  Enter 'help' to view a list of available commands.\r\n\r\n",
        xWriteBufferLen);
        xReturn = pdFALSE;
    }

    return xReturn;
}
/*-----------------------------------------------------------*/

char * MidCLI_GetOutputBuffer (void)
{
    return cOutputBuffer;
}
/*-----------------------------------------------------------*/

const char * MidCLI_GetParameter(const char *pcCommandString,
                                 UBaseType_t uxWantedParameter,
                                 BaseType_t *pxParameterStringLength)
{
    UBaseType_t uxParametersFound = 0;
    const char *pcReturn = NULL;

    *pxParameterStringLength = 0;

    while (uxParametersFound < uxWantedParameter)
    {
        /* Index the character pointer past the current word.  If this is the start
        of the command string then the first word is the command itself. */
        while (((*pcCommandString) != 0x00) && ((*pcCommandString) != ' '))
        {
            pcCommandString++;
        }

        /* Find the start of the next string. */
        while (((*pcCommandString) != 0x00) && ((*pcCommandString) == ' '))
        {
            pcCommandString++;
        }

        /* Was a string found? */
        if (*pcCommandString != 0x00)
        {
            /* Is this the start of the required parameter? */
            uxParametersFound++;
            if (uxParametersFound == uxWantedParameter)
            {
                /* How long is the parameter? */
                pcReturn = pcCommandString;
                while (((*pcCommandString) != 0x00) && ((*pcCommandString) != ' '))
                {
                    (*pxParameterStringLength)++;
                    pcCommandString++;
                }

                if (*pxParameterStringLength == 0)
                {
                    pcReturn = NULL;
                }
                break;
            }
        }
        else
        {
            break;
        }
    }

    return pcReturn;
}
/*-----------------------------------------------------------*/

static BaseType_t prvHelpCommand(char *pcWriteBuffer, 
                                 size_t xWriteBufferLen,
                                 const char *pcCommandString)
{
    static const CLI_Definition_List_Item_t * pxCommand = NULL;
    BaseType_t xReturn;

    (void) pcCommandString;

    if (pxCommand == NULL)
    {
        /* Reset the pxCommand pointer back to the start of the list. */
        pxCommand = &xRegisteredCommands;
    }

    /* Return the next command help string, before moving the pointer on to
    the next command in the list. */
    strncpy (pcWriteBuffer, pxCommand->pxCommandLineDefinition->pcHelpString,
    xWriteBufferLen);
    pxCommand = pxCommand->pxNext;

    if (pxCommand == NULL)
    {
        /* There are no more commands in the list, so there will be no more
        strings to return after this one and pdFALSE should be returned. */
        xReturn = pdFALSE;
    }
    else
    {
        xReturn = pdTRUE;
    }

    return xReturn;
}
/*-----------------------------------------------------------*/

static int8_t prvGetNumberOfParameters (const char *pcCommandString)
{
    int8_t cParameters = 0;
    BaseType_t xLastCharacterWasSpace = pdFALSE;

    /* Count the number of space delimited words in pcCommandString. */
    while (*pcCommandString != 0x00)
    {
        if ((*pcCommandString) == ' ')
        {
            if (xLastCharacterWasSpace != pdTRUE)
            {
                cParameters++;
                xLastCharacterWasSpace = pdTRUE;
            }
        }
        else
        {
            xLastCharacterWasSpace = pdFALSE;
        }

        pcCommandString++;
    }

    /* If the command string ended with spaces, then there will have been too
    many parameters counted. */
    if (xLastCharacterWasSpace == pdTRUE)
    {
        cParameters--;
    }

    /* The value returned is one less than the number of space delimited words,
    as the first word should be the command itself. */
    return cParameters;
}

/**
 * @brief  Initialization  shell uart
 * @param  NA
 * @retval HAL_OK in case of success, an error code otherwise
 */

HAL_StatusTypeDef mid_CliInit(void)
{
    return shellDrv.Init (&cmuPort);
}

/**
 * @brief  Get a input form shell uart
 * @param  NA
 * @retval  value for uart  NULL for  no value
 */
char mid_CliGetChar (void)
{
    return shellDrv.ReadCharBlock();
}
/**
 * @brief  Get a input form shell uart none blocking
 * @param  NA
 * @retval  value for uart  NULL for  no value
 */
char mid_CliGetCharNonBlocking (void)
{
    return shellDrv.ReadAchar();
}
/**
 * @brief  Get a string input form shell uart none blocking
 * @param  NA
 * @retval  value for uart  NULL for  no value
 */
MID_STATUS_TYPE_T mid_CliGetChars (uint8_t *pBuf,uint32_t length)
{
    return (MID_STATUS_TYPE_T)shellDrv.Read(pBuf,length);
}
/**
 * @brief  Send a block char to uart
 * @param  pTxBuf:the chars buffer,length: 多少个数据
 * @retval HAL_OK in case of success, an error code otherwise
 */
HAL_StatusTypeDef mid_CliSendData (uint8_t *pTxBuf, uint16_t length)
{
    HAL_StatusTypeDef result = HAL_ERROR;

    shellDrv.GetAccess ();
    result = shellDrv.Write (pTxBuf, length);
    shellDrv.ReleaseAccess ();

    return result;
}
/**
 * @brief  发送字符串
 * @param  pTxBuf：字符串的缓存
 * @retval HAL_OK in case of success, an error code otherwise
 */
HAL_StatusTypeDef mid_CliSendString (char *pTxBuf)
{
    HAL_StatusTypeDef result = HAL_ERROR;

    shellDrv.GetAccess ();
    result = shellDrv.Write((uint8_t *)pTxBuf, strlen (pTxBuf));
    shellDrv.ReleaseAccess ();

    return result;
}
