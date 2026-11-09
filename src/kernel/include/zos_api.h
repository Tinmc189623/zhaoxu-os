#ifndef ZHAOXU_ZOS_API_H
#define ZHAOXU_ZOS_API_H

/*
 * Rule 24: the system ABI/API presented by Zhaoxu OS.
 * Applications target this banner; the kernel render API and the ADE/DWM
 * desktop API are both part of it.
 */
#define ZOS_API_NAME    "Zhaoxu Operating System Application Programming Interface"
#define ZOS_API_VERSION "1.0"
#define ZOS_API_BANNER  ZOS_API_NAME " Version " ZOS_API_VERSION

#endif
