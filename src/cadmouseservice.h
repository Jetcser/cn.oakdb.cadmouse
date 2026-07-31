#ifndef CADMOUSESERVICE_H
#define CADMOUSESERVICE_H

#include "cadmouseconfig.h"

/**
 * 无界面服务逻辑：心跳维持、接收器初始化。
 * 由独立二进制 cadmouse-helper 调用。
 */
namespace CadMouseService {

int runHeartbeat(const CadMouseServiceConfig &cfg);
int runInitReceiver(const CadMouseServiceConfig &cfg);

} // namespace CadMouseService

#endif // CADMOUSESERVICE_H
