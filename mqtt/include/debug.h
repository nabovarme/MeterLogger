/*
 * debug.h
 *
 *  Created on: Dec 4, 2014
 *      Author: Minh
 */

//#define PRINTF_DEBUG

#ifndef USER_DEBUG_H_
#define USER_DEBUG_H_

#ifndef INFO
#ifdef PRINTF_DEBUG
#define INFO os_printf
#else
#define INFO(...) /**/
#endif
#endif

#endif /* USER_DEBUG_H_ */
