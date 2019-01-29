# Project Title

HotPU backup and restore tool for MySQL

## Getting Started

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes. See deployment for notes on how to deploy the project on a live system.

## Prerequisites

HotPU's backup and restore tool is developed and tested on MySQL 5.7.18. Code can be compiled under 5.7.18 compatible versions.

## Installing

Copy file to MySQL source directory "client". Add the following content to "CMakeList.txt".

```shell
MYSQL_ADD_EXECUTABLE(htp_xport htp_xport.cc)
TARGET_LINK_LIBRARIES(htp_xport mysqlclient)
IF(UNIX)
  TARGET_LINK_LIBRARIES(htp_xport ${EDITLINE_LIBRARY})
ENDIF(UNIX)
```

## manual 

looking for help for MS word

## Authors

jiangyuxiang@hotpu.cn 
haoguoqing@hotpu.cn

## License

This project is licensed under the GPL License
