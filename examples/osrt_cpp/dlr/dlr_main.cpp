/*
Copyright (c) 2020 – 2021 Texas Instruments Incorporated

All rights reserved not granted herein.

Limited License.

Texas Instruments Incorporated grants a world-wide, royalty-free, non-exclusive
license under copyrights and patents it now or hereafter owns or controls to
make, have made, use, import, offer to sell and sell ("Utilize") this software
subject to the terms herein.  With respect to the foregoing patent license,
such license is granted  solely to the extent that any such patent is necessary
to Utilize the software alone.  The patent license shall not apply to any
combinations which include this software, other than combinations with devices
manufactured by or for TI (“TI Devices”).  No hardware patent is licensed
hereunder.

Redistributions must preserve existing copyright notices and reproduce this
license (including the above copyright notice and the disclaimer and
(if applicable) source code license limitations below) in the documentation
and/or other materials provided with the distribution

Redistribution and use in binary form, without modification, are permitted
provided that the following conditions are met:

*	No reverse engineering, decompilation, or disassembly of this software is
    permitted with respect to any software provided in binary form.

*	any redistribution and use are licensed by TI for use only with TI Devices.

*	Nothing shall obligate TI to provide you with source code for the software
    licensed and provided to you in object code.

If software source code is provided to you, modification and redistribution of
the source code are permitted provided that the following conditions are met:

*	any redistribution and use of the source code, including any resulting
    derivative works, are licensed by TI for use only with TI Devices.

*	any redistribution and use of any object code compiled from the source code
    and any resulting derivative works, are licensed by TI for use only with TI
    Devices.

Neither the name of Texas Instruments Incorporated nor the names of its
suppliers may be used to endorse or promote products derived from this software
without specific prior written permission.

DISCLAIMER.

THIS SOFTWARE IS PROVIDED BY TI AND TI’S LICENSORS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
EVENT SHALL TI AND TI’S LICENSORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/
#include "dlr_main.h"

namespace dlr
{
    namespace main
    {
        /**
         *  \brief  get the  output tensors
         *  \param  outputs
         *  \param  num_outputs num of outputs
         *  \param  model DLR model
         * @returns void
         */
        template <class T>
        int fetchOutputTensors(std::vector<std::vector<T>> &outputs,
                               int num_outputs,
                               DLRModelHandle model)
        {

            for (int i = 0; i < num_outputs; i++)
            {
                int64_t cur_size = 0;
                int cur_dim = 0;
                GetDLROutputSizeDim(&model, i, &cur_size, &cur_dim);
                std::vector<T> output(cur_size, 0);
                outputs.push_back(output);
            }
            for (int i = 0; i < num_outputs; i++)
            {
                if (GetDLROutput(&model, i, outputs[i].data()) != 0)
                {
                    LOG_ERROR("Could not get output:%s", std::to_string(i));
                    return RETURN_FAIL;
                }
            }
            return RETURN_SUCCESS;
        }

        template int fetchOutputTensors<float>(std::vector<std::vector<float>> &outputs,
                                               int num_outputs,
                                               DLRModelHandle model);
        template int fetchOutputTensors<int64_t>(std::vector<std::vector<int64_t>> &outputs,
                                                 int num_outputs,
                                                 DLRModelHandle model);

        /**
         *  \brief  get the tensor type of input and output
         *  \param  index index of tensor to find the type
         *  \param  isInput bool to determine input/output tensor to fetch teh details
         *          for
         *  \param  model DLR model
         * @returns const char* comataining the type name
         */
        const char *getTensorType(int index, bool isInput, DLRModelHandle model)
        {
            const char *output_type_feild[] = {};
            const char **output_type = &output_type_feild[0];
            if (isInput)
            {
                /* determine the output type */
                GetDLRInputType(&model, index, output_type);
            }
            /* determine the output tensor type at index  */
            else
            {
                /* determine the output type */
                GetDLROutputType(&model, index, output_type);
            }
            return *output_type;
        }

        /**
         *  \brief  prepare the classification result inplace
         *  \param  img cv image to do inplace transform
         *  \param  s settings struct pointer
         *  \param  model DLR model Handle
         *  \param  num_outputs
         * @returns int status
         */
        int prepClassificationResult(cv::Mat *img, Settings *s, DLRModelHandle model, int num_outputs)
        {
            LOG_INFO("preparing classification result \n");
            const float threshold = 0.001f;
            std::vector<std::pair<float, int>> top_results;
            /* get tensor type */
            const char *output_type = getTensorType(0, false, model);
            if (!strcmp(output_type, "float32"))
            {
                std::vector<std::vector<float>> outputs;
                if (RETURN_FAIL == fetchOutputTensors<float>(outputs, num_outputs, model))
                    return RETURN_FAIL;
                float *tensor_op_array = outputs[0].data();
                /*assuming 1 output vector */
                getTopN<float>(outputs[0].data(),
                               1000, s->number_of_results, threshold,
                               &top_results, true);
                std::vector<std::string> labels;
                size_t label_count;

                if (readLabelsFile(s->labels_file_path, &labels, &label_count) != 0)
                {
                    LOG_ERROR("Failed to load labels file\n");
                    return RETURN_FAIL;
                }

                int outputoffset;
                int output_size = outputs[0].size();
                if (output_size == 1001)
                    outputoffset = 0;
                else
                    outputoffset = 1;
                for (const auto &result : top_results)
                {
                    const float confidence = result.first;
                    const int index = result.second;

                    LOG_INFO("%f: %d %s\n", confidence, index, labels[index + outputoffset].c_str());
                }
                int num_results = s->number_of_results;
                (*img).data = overlayTopNClasses((*img).data, top_results, &labels, (*img).cols, (*img).rows, num_results);
            }
            else
            {
                LOG_ERROR("output type not supported %s\n", *output_type);
                return RETURN_FAIL;
            }
            return RETURN_SUCCESS;
        }

    
        /**
         *  \brief  prepare the segmentation result inplace
         *  \param  img cv image to do inplace transform
         *  \param  model DLR model Handle
         *  \param  num_outputs
         *  \param  mdoelInfo pointer to modelInfo
         *  \param  wanted_width
         *  \param  wanted_height
         * @returns int status
         */
        int prepSegResult(cv::Mat *img, DLRModelHandle model, int num_outputs,
                          ModelInfo *modelInfo, int wanted_width, int wanted_height)
        {
            LOG_INFO("preparing segmentation result \n");
            float alpha = modelInfo->m_postProcCfg.alpha;
            /* determining the shape of output0
            assuming 1 output of shape [1 , 1 , width , height]*/
            int64_t output_size = 0;
            int output_dim = 0;
            GetDLROutputSizeDim(&model, 0, &output_size, &output_dim);
            int64_t output_shape[output_dim];
            GetDLROutputShape(&model, 0, output_shape);
            /* if indata and out data is diff resize the image
            check whether img need to be resized based on out data
            asssuming out put format [1,1,,width,height]*/
            if (wanted_height != output_shape[2] || wanted_width != output_shape[3])
            {
                LOG_INFO("Resizing image to match output dimensions\n");
                wanted_height = output_shape[2];
                wanted_width = output_shape[3];
                cv::resize((*img), (*img), cv::Size(wanted_width, wanted_height), 0, 0, cv::INTER_AREA);
            }
            /* determine the output type */
            const char *output_type = getTensorType(0, false, model);
            if (!strcmp(output_type, "int64"))
            {
                std::vector<std::vector<int64_t>> outputs;
                fetchOutputTensors<int64_t>(outputs, num_outputs, model);
                (*img).data = blendSegMask<int64_t>((*img).data, outputs[0].data(), (*img).cols, (*img).rows, wanted_width, wanted_height, alpha);
            }
            else if (!strcmp(output_type, "float32"))
            {
                std::vector<std::vector<float>> outputs;
                fetchOutputTensors<float>(outputs, num_outputs, model);
                (*img).data = blendSegMask<float>((*img).data, outputs[0].data(), (*img).cols, (*img).rows, wanted_width, wanted_height, alpha);
            }
            else
            {
                LOG_ERROR("output type not supported %s\n", *output_type);
                return RETURN_FAIL;
            }
            return RETURN_SUCCESS;
        }

        /**
         *  \brief  Actual infernce happening
         *  \param  ModelInfo YAML parsed model info
         *  \param  Settings user input options  and default values of setting if any
         * @returns int
         */
        int runInference(ModelInfo *modelInfo, Settings *s)
        {
            int num_outputs, num_inputs;
            /*Initial inference time*/
            double fp_ms_avg = 0.0;
            DLRModelHandle model;
            int device_type;
            if (s->device_type == "cpu")
            {
                device_type = 1;
            }
            else if (s->device_type == "gpu")
            {
                device_type = 2;
            }
            else
            {
                LOG_ERROR("device type not supported: %s", s->device_type.c_str());
                return RETURN_FAIL;
            }
            if (CreateDLRModel(&model, modelInfo->m_infConfig.artifactsPath.c_str(), device_type, 0) != 0)
            {
                LOG_ERROR("Could not load DLR Model\n");
                return RETURN_FAIL;
            }

            /*input vector infering -assuming single input*/
            GetDLRNumInputs(&model, &num_inputs);
            if (num_inputs != 1)
            {
                LOG_ERROR("Model with more than one input not supported\n");
                return RETURN_FAIL;
            }
            /* Query input name. */
            const char *input_name{nullptr};
            int status = GetDLRInputName(&model,
                                         0,
                                         &input_name);

            if (status < 0)
            {
                LOG_ERROR("GetDLRInputName(0) failed. Error [%s].\n",
                          DLRGetLastError());
                return RETURN_FAIL;
            }
            LOG_INFO("%s :input name\n", input_name);
            int64_t input_size = 0;
            int input_dim = 0;
            GetDLRInputSizeDim(&model, 0, &input_size, &input_dim);
            int64_t input_shape[input_dim];
            GetDLRInputShape(&model, 0, input_shape);
            int wanted_batch_size = input_shape[0];
            int wanted_height = modelInfo->m_preProcCfg.outDataHeight;
            int wanted_width = modelInfo->m_preProcCfg.outDataWidth;
            int wanted_channels = modelInfo->m_preProcCfg.numChans;
            if (modelInfo->m_preProcCfg.dataLayout == "NHWC")
            {
                if (wanted_channels != input_shape[3])
                {
                    LOG_INFO("missmatch in YAML parsed wanted channels:%d and model channels:%d\n", wanted_channels, input_shape[3]);
                }
                if (wanted_height != input_shape[1])
                {
                    LOG_INFO("missmatch in YAML parsed wanted height:%d and model height:%d\n", wanted_height, input_shape[1]);
                }
                if (wanted_width != input_shape[2])
                {
                    LOG_INFO("missmatch in YAML parsed wanted width:%d and model width:%d\n", wanted_width, input_shape[2]);
                }
            }
            else if (modelInfo->m_preProcCfg.dataLayout == "NCHW")
            {
                if (wanted_channels != input_shape[1])
                {
                    LOG_INFO("missmatch in YAML parsed wanted channels:%d and model:%d\n", wanted_channels, input_shape[1]);
                }
                if (wanted_height != input_shape[2])
                {
                    LOG_INFO("missmatch in YAML parsed wanted height:%d and model:%d\n", wanted_height, input_shape[2]);
                }
                if (wanted_width != input_shape[3])
                {
                    LOG_INFO("missmatch in YAML parsed wanted width:%d and model:%d\n", wanted_width, input_shape[3]);
                    ;
                }
            }
            else
            {
                LOG_ERROR("data layout not supported\n");
                return RETURN_FAIL;
            }

            LOG_INFO("Inference call started...\n");
            cv::Mat img;
            float *image_data = (float *)malloc(sizeof(float) * wanted_height * wanted_width * wanted_channels);
            if (image_data == NULL)
            {
                LOG_ERROR("could not allocate space for image data \n");
                return RETURN_FAIL;
            }
            const char *input_type_feild[] = {};
            const char **input_type = &input_type_feild[0];

            GetDLRInputType(&model, 0, input_type);
            if (!strcmp(*input_type, "float32"))
            {
                img = preprocImage<float>(s->input_bmp_path, image_data, modelInfo->m_preProcCfg);
            }
            else
            {
                LOG_ERROR("cannot handle input type %s yet", *input_type);
                return RETURN_FAIL;
            }
            LOG_INFO("Classifying input:%s\n", s->input_bmp_path.c_str());

            /*Running inference */
            if (SetDLRInput(&model, input_name, input_shape, image_data, 4) != 0)
            {
                LOG_ERROR("Could not set input:%s\n", input_name);
                return RETURN_FAIL;
            }
            if (RunDLRModel(&model) != 0)
            {
                LOG_ERROR("Could not run\n");
            }
            /*output vector infering*/
            GetDLRNumOutputs(&model, &num_outputs);
            if (modelInfo->m_preProcCfg.taskType == "classification")
            {
                if (RETURN_FAIL == prepClassificationResult(&img, s, model, num_outputs))
                    return RETURN_FAIL;
            }
            else if (modelInfo->m_preProcCfg.taskType == "detection")
            {

                /*store tensor_shape info of op tensors in arr
               to avaoid recalculation*/
                vector<vector<int64_t>> tensor_shapes_vec;
                vector<int64_t> tensor_size_vec;
                vector<vector<float>> f_tensor_unformatted;

                for (size_t i = 0; i < num_outputs; i++)
                {
                    int64_t output_size = 0;
                    int output_dim = 0;
                    int64_t output_shape[output_dim];
                    GetDLROutputSizeDim(&model, i, &output_size, &output_dim);
                    GetDLROutputShape(&model, i, output_shape);
                    vector<int64_t> tensor_shape;
                    tensor_size_vec.push_back(output_size);
                    for (size_t k = 0; k < output_dim; k++)
                    {
                        tensor_shape.push_back(output_shape[k]);
                    }

                    tensor_shapes_vec.push_back(tensor_shape);
                }
                /* num of detection in op tensor  assumes the size of
                1st tensor*/
                int64_t nboxes;
                int output_dim = 0;
                GetDLROutputSizeDim(&model, 0, &nboxes, &output_dim);

                for (size_t i = 0; i < num_outputs; i++)
                {
                    /* temp vector to store converted ith tensor */
                    vector<float> f_tensor;
                    /* shape of the ith tensor*/
                    vector<int64_t> tensor_shape = tensor_shapes_vec[i];
                    /* type of the ith tensor*/
                    const char *tensor_type = getTensorType(i, false, model);
                    /* num of values in ith tensor*/
                    int num_val_tensor;
                     /*Extract the last dimension from each of the output
                    tensors.last dimension will give the number of values present in
                    given tensor. Need to ignore all dimensions with value 1 since it
                    does not actually add a dimension */
                    auto temp = tensor_shape;
                    for(auto it = temp.begin() ; it < temp.end();it++)
                    {
                        if((*it) == 1){
                            temp.erase(it);
                            it--;
                        }
                    }
                    if (temp.size() == 1)
                        num_val_tensor = 1;
                    else{
                        num_val_tensor = temp[temp.size() - 1];
                    }

                    /*convert tensor to float vector*/
                    if (!strcmp(tensor_type, "float32"))
                    {
                        
                        std::vector<float> output(nboxes*num_val_tensor,0);
                        if (GetDLROutput(&model, i, output.data()) != 0)
                        {
                            LOG_ERROR("Could not get output:%d", i);
                            return RETURN_FAIL;
                        }
                        /* already in float vec no need to convert */
                    std::copy(output.begin(), output.end(),back_inserter(f_tensor));
                        
                    }
                    else if (tensor_type == "int64")
                    {
                        std::vector<int64_t> output(nboxes*num_val_tensor,0);
                        if (GetDLROutput(&model, i, output.data()) != 0)
                        {
                            LOG_ERROR("Could not get output:%d", i);
                            return RETURN_FAIL;
                        }
                        std::copy(output.begin(), output.end(),back_inserter(f_tensor));
                    }
                    else
                    {
                        LOG_ERROR("out tensor data type not supported: %s\n",tensor_type );
                        return RETURN_FAIL;
                    }
                    /*append all output tensors in to single vector<vector<float>*/
                    for (size_t j = 0; j < nboxes; j++)
                    {
                        vector<float> temp;
                        for (size_t k = 0; k < num_val_tensor; k++)
                        {
                            temp.push_back(f_tensor[j * num_val_tensor + k]);
                        }
                        f_tensor_unformatted.push_back(temp);
                    }
                }
                /* Updating the format coz format is NULL in param.yaml
                format [x1y1 x2y2 label score]*/
                modelInfo->m_postProcCfg.formatter = {2,3,4,5,0,1};
                modelInfo->m_postProcCfg.formatterName = "DetectionBoxSL2BoxLS";
                
                if (RETURN_FAIL == prepDetectionResult(&img, &f_tensor_unformatted, tensor_shapes_vec, modelInfo, num_outputs,nboxes))
                    return RETURN_FAIL;
            }
            else if (modelInfo->m_preProcCfg.taskType == "segmentation")
            {
                if (RETURN_FAIL == prepSegResult(&img, model, num_outputs, modelInfo, wanted_width, wanted_height))
                    return RETURN_FAIL;
            }
            cv::cvtColor(img, img, cv::COLOR_RGB2BGR);
            char filename[500];
            strcpy(filename, "test_data/");
            strcat(filename, "cpp_inference_out");
            strcat(filename, modelInfo->m_preProcCfg.modelName.c_str());
            strcat(filename, ".jpg");
            bool check = cv::imwrite(filename, img);
            if (check == false)
            {
                LOG_ERROR("Saving the image, FAILED\n");
            }
            LOG_INFO("Done\n");
        }
    } // namespace main
} // namespace dlr

int main(int argc, char **argv)
{
    Settings s;
    if (parseArgs(argc, argv, &s) == RETURN_FAIL)
    {
        LOG_ERROR("Failed to parse the args\n");
        return RETURN_FAIL;
    }
    dumpArgs(&s);
    logSetLevel((LogLevel)s.log_level);
    /* Parse the input configuration file */
    ModelInfo model(s.model_zoo_path);
    if (model.initialize() == RETURN_FAIL)
    {
        LOG_ERROR("Failed to initialize model\n");
        return RETURN_FAIL;
    }
    if (dlr::main::runInference(&model, &s) == RETURN_FAIL)
    {
        LOG_ERROR("Failed to run runInference\n");
        return RETURN_FAIL;
    }
    return RETURN_SUCCESS;
}
