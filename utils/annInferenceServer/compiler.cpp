#include "compiler.h"
#include "netutil.h"
#include "common.h"

int runCompiler(int sock, Arguments * args, std::string& clientName, InfComCommand * cmdMode)
{
    //////
    /// \brief get and check parameters
    ///
    int dimInput[3] = { cmdMode->data[1], cmdMode->data[2], cmdMode->data[3] };
    int modelType = cmdMode->data[4];
    int reverseInputChannelOrder = 0;
    if(dimInput[0] <= 0 || dimInput[1] <= 0 || dimInput[2] != 3) {
        dumpCommand("X", *cmdMode);
        return error_close(sock, "unsupported input dimensions %dx%dx%d", dimInput[2], dimInput[1], dimInput[0]);
    }
    if(modelType != 0) {
        dumpCommand("X", *cmdMode);
        return error_close(sock, "unsupported compiler model type = %d", modelType);
    }
    if(!strcmp(cmdMode->message, "BGR")) {
        reverseInputChannelOrder = 1;
    }
    else if(cmdMode->message[0] != '\0') {
        return error_close(sock, "unsupported compiler options [%s]", cmdMode->message);
    }

    //////
    /// \brief generate new model name for this download and create model folders
    ///
    char modelName[64];
    sprintf(modelName, "upload/model-%08d", args->getNextModelUploadCounter());
    std::string modelFolder = args->getConfigurationDir() + "/" + modelName;
    std::string buildFolder = modelFolder + "/build";
    if(mkdir(modelFolder.c_str(), 0700) < 0 || mkdir(buildFolder.c_str(), 0700) < 0) {
        fatal("unable to create folders: %s and %s", modelFolder.c_str(), buildFolder.c_str());
    }

    //////
    /// \brief download model files
    ///
    int modelFileCommand[2] = {
        INFCOM_CMD_SEND_MODELFILE1, INFCOM_CMD_SEND_MODELFILE2
    };
    for(int i = 0; i < 2; i++) {
        // send INFCOM_CMD_SEND_MODELFILE1 or INFCOM_CMD_SEND_MODELFILE2
        InfComCommand cmd = {
            INFCOM_MAGIC, modelFileCommand[i], { 0 }, { 0 }
        };
        ERRCHK(sendCommand(sock, cmd, clientName));
        // wait for reply with same command and fileSize in bytes
        ERRCHK(recvCommand(sock, cmd, clientName, modelFileCommand[i]));
        // receive the modelFile byte stream
        int size = cmd.data[0];
        char * byteStream = nullptr;
        if(size > 0) {
            byteStream = new char [size];
            int remaining = size;
            while(remaining > 0) {
                int n = recv(sock, byteStream + size - remaining, remaining, 0);
                if(n < 1)
                    break;
                remaining -= n;
            }
            if(remaining > 0) {
                delete[] byteStream;
                return error_close(sock, "INFCOM_CMD_SEND_MODELFILE%d: could only received %d bytes out of %d bytes from %s", i + 1, size - remaining, size, clientName.c_str());
            }
            int eofMarker = 0;
            recv(sock, &eofMarker, sizeof(eofMarker), 0);
            if(eofMarker != INFCOM_EOF_MARKER) {
                delete[] byteStream;
                return error_close(sock, "INFCOM_CMD_SEND_MODELFILE%d: eofMarker 0x%08x (incorrect) from %s", i + 1, eofMarker, clientName.c_str());
            }
        }
        if(byteStream) {
            std::string fileName = modelFolder + ((i == 0) ? "/deploy.prototxt" : "/weights.caffemodel");
            info("saving INFCOM_CMD_SEND_MODELFILE%d with %d bytes from %s into %s", i + 1, size, clientName.c_str(), fileName.c_str());
            FILE * fp = fopen(fileName.c_str(), "wb");
            if(fp) {
                fwrite(byteStream, 1, size, fp);
                fclose(fp);
            }
            else {
                fatal("unable to create: %s", fileName.c_str());
            }
            delete[] byteStream;
        }
    }

    //////
    /// \brief start inference generator
    ///
    InfComCommand cmdUpdate = {
        INFCOM_MAGIC, INFCOM_CMD_COMPILER_STATUS, { 0 }, { 0 }
    };
    int status = chdir(modelFolder.c_str());
    if(status < 0) {
        cmdUpdate.data[0] = -1;
        sprintf(cmdUpdate.message, "model folder not found");
        ERRCHK(sendCommand(sock, cmdUpdate, clientName));
        ERRCHK(recvCommand(sock, cmdUpdate, clientName, INFCOM_CMD_COMPILER_STATUS));
        return error_close(sock, "chdir('%s') failed", modelFolder.c_str());
    }
    // step-1: run inference generator
    cmdUpdate.data[0] = 0;
    cmdUpdate.data[1] = 1;
    sprintf(cmdUpdate.message, "inference_generator started ...");
    ERRCHK(sendCommand(sock, cmdUpdate, clientName));
    ERRCHK(recvCommand(sock, cmdUpdate, clientName, INFCOM_CMD_COMPILER_STATUS));
    // step-1.1: inference_generator on caffemodel for weights
    std::string command = "inference_generator weights.caffemodel";
    command += " " + std::to_string(args->getBatchSize())
            +  " " + std::to_string(dimInput[2])
            +  " " + std::to_string(dimInput[1])
            +  " " + std::to_string(dimInput[0])
            +  " >inference_generator.log";
    info("executing: %% %s", command.c_str());
    status = system(command.c_str());
    cmdUpdate.data[0] = (status != 0) ? -2 : 0;
    cmdUpdate.data[1] = 25;
    sprintf(cmdUpdate.message, "inference_generator weights.caffemodel completed (%d)", status);
    ERRCHK(sendCommand(sock, cmdUpdate, clientName));
    ERRCHK(recvCommand(sock, cmdUpdate, clientName, INFCOM_CMD_COMPILER_STATUS));
    if(status) {
        return error_close(sock, "command-failed(%d): %s", status, command.c_str());
    }
    // step-1.2: inference_generator on prototxt for network structure
    command = "inference_generator deploy.prototxt";
    command += " " + std::to_string(args->getBatchSize())
            +  " " + std::to_string(dimInput[2])
            +  " " + std::to_string(dimInput[1])
            +  " " + std::to_string(dimInput[0])
            +  " >>inference_generator.log";
    info("executing: %% %s", command.c_str());
    status = system(command.c_str());
    cmdUpdate.data[0] = (status != 0) ? -3 : 0;
    cmdUpdate.data[1] = 50;
    sprintf(cmdUpdate.message, "inference_generator deploy.prototxt completed (%d)", status);
    ERRCHK(sendCommand(sock, cmdUpdate, clientName));
    ERRCHK(recvCommand(sock, cmdUpdate, clientName, INFCOM_CMD_COMPILER_STATUS));
    if(status) {
        return error_close(sock, "command-failed(%d): %s", status, command.c_str());
    }
    // step-2: get output dimensions
    int dimOutput[3] = { 0 };
    FILE * fp = fopen("inference_generator.log", "r");
    if(!fp) {
        return error_close(sock, "unable to open: inference_generator.log");
    }
    char line[1024];
    while(fgets(line, sizeof(line), fp) == line) {
        if(!strncmp(line, "#OUTPUT-TENSOR: ", 16)) {
            sscanf(line, "%*s%*s%*s%d%d%d", &dimOutput[2], &dimOutput[1], &dimOutput[0]);
        }
    }
    fclose(fp);
    info("found output tensor dimensions %dx%dx%d for %s", dimOutput[2], dimOutput[1], dimOutput[0], modelName);

    // step-3: build the module
    status = chdir(buildFolder.c_str());
    if(status < 0) {
        cmdUpdate.data[0] = -1;
        sprintf(cmdUpdate.message, "build folder not found");
        ERRCHK(sendCommand(sock, cmdUpdate, clientName));
        ERRCHK(recvCommand(sock, cmdUpdate, clientName, INFCOM_CMD_COMPILER_STATUS));
        return error_close(sock, "chdir('%s') failed", buildFolder.c_str());
    }
    command = "cmake .. >../cmake.log";
    info("executing: %% %s", command.c_str());
    status = system(command.c_str());
    std::string makefilePath = buildFolder + "/Makefile";
    struct stat sbufMakefile = { 0 };
    if(stat(makefilePath.c_str(), &sbufMakefile) != 0) {
        status = 1;
        warning("could not locate cmake output: %s", makefilePath.c_str());
    }
    cmdUpdate.data[0] = (status != 0) ? -4 : 0;
    cmdUpdate.data[1] = 75;
    sprintf(cmdUpdate.message, "cmake completed (status = %d)", status);
    ERRCHK(sendCommand(sock, cmdUpdate, clientName));
    ERRCHK(recvCommand(sock, cmdUpdate, clientName, INFCOM_CMD_COMPILER_STATUS));
    if(status) {
        return error_close(sock, "command-failed(%d): %s", status, command.c_str());
    }
    command = "make >../make.log";
    info("executing: %% %s", command.c_str());
    status = system(command.c_str());
    cmdUpdate.data[0] = (status != 0) ? -5 : 0;
    cmdUpdate.data[1] = 99;
    sprintf(cmdUpdate.message, "make completed (status = %d)", status);
    ERRCHK(sendCommand(sock, cmdUpdate, clientName));
    ERRCHK(recvCommand(sock, cmdUpdate, clientName, INFCOM_CMD_COMPILER_STATUS));
    if(status) {
        return error_close(sock, "command-failed(%d): %s", status, command.c_str());
    }
    std::string modulePath = buildFolder + "/" + MODULE_LIBNAME;
    struct stat sbufModule = { 0 };
    if(stat(modulePath.c_str(), &sbufModule) != 0) {
        cmdUpdate.data[0] = -6;
        sprintf(cmdUpdate.message, "couldn't locate generated module");
        ERRCHK(sendCommand(sock, cmdUpdate, clientName));
        ERRCHK(recvCommand(sock, cmdUpdate, clientName, INFCOM_CMD_COMPILER_STATUS));
        return error_close(sock, "could not locate built module: %s", modulePath.c_str());
    }

    // step-final: send completion status message
    cmdUpdate.data[0] = 1;
    cmdUpdate.data[1] = 100;
    cmdUpdate.data[2] = dimOutput[0];
    cmdUpdate.data[3] = dimOutput[1];
    cmdUpdate.data[4] = dimOutput[2];
    sprintf(cmdUpdate.message, "%s", modelName);
    ERRCHK(sendCommand(sock, cmdUpdate, clientName));
    ERRCHK(recvCommand(sock, cmdUpdate, clientName, INFCOM_CMD_COMPILER_STATUS));

    // add uploaded model to args
    std::tuple<std::string,int,int,int,int,int,int,int>
            ann(modelName, dimInput[0], dimInput[1], dimInput[2], dimOutput[0], dimOutput[1], dimOutput[2], reverseInputChannelOrder);
    args->addUploadedConfig(ann);
    info("added uploaded model name:%s input:%dx%dx%d output:%dx%dx%d reverseInputChannelOrder:%d",
            modelName, dimInput[2], dimInput[1], dimInput[0], dimOutput[2], dimOutput[1], dimOutput[0], reverseInputChannelOrder);

    // create module configuration file
    std::string annModuleConfigFile = args->getConfigurationDir() + "/" + modelName + "/" + MODULE_CONFIG;
    fp = fopen(annModuleConfigFile.c_str(), "w");
    if(fp) {
        fprintf(fp, "%s\n%d %d %d\n%d %d %d\n%d\n", modelName,
                       dimInput[0], dimInput[1], dimInput[2],
                       dimOutput[0], dimOutput[1], dimOutput[2],
                       reverseInputChannelOrder);
        fclose(fp);
    }

    // send and wait for INFCOM_CMD_DONE message
    InfComCommand reply = {
        INFCOM_MAGIC, INFCOM_CMD_DONE, { 0 }, { 0 }
    };
    ERRCHK(sendCommand(sock, reply, clientName));
    ERRCHK(recvCommand(sock, reply, clientName, INFCOM_CMD_DONE));

    return 0;
}
