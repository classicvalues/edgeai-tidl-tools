/*
Copyright (c) 2020 – 2021 Texas Instruments Incorporated

All rights reserved not granted herein.

Limited License.  

Texas Instruments Incorporated grants a world-wide, royalty-free, non-exclusive license under copyrights and patents it now or hereafter owns or controls to make, have made, use, import, offer to sell and sell ("Utilize") this software subject to the terms herein.  With respect to the foregoing patent license, such license is granted  solely to the extent that any such patent is necessary to Utilize the software alone.  The patent license shall not apply to any combinations which include this software, other than combinations with devices manufactured by or for TI (“TI Devices”).  No hardware patent is licensed hereunder.

Redistributions must preserve existing copyright notices and reproduce this license (including the above copyright notice and the disclaimer and (if applicable) source code license limitations below) in the documentation and/or other materials provided with the distribution

Redistribution and use in binary form, without modification, are permitted provided that the following conditions are met:

*	No reverse engineering, decompilation, or disassembly of this software is permitted with respect to any software provided in binary form.

*	any redistribution and use are licensed by TI for use only with TI Devices.

*	Nothing shall obligate TI to provide you with source code for the software licensed and provided to you in object code.

If software source code is provided to you, modification and redistribution of the source code are permitted provided that the following conditions are met:

*	any redistribution and use of the source code, including any resulting derivative works, are licensed by TI for use only with TI Devices.

*	any redistribution and use of any object code compiled from the source code and any resulting derivative works, are licensed by TI for use only with TI Devices.

Neither the name of Texas Instruments Incorporated nor the names of its suppliers may be used to endorse or promote products derived from this software without specific prior written permission.

DISCLAIMER.

THIS SOFTWARE IS PROVIDED BY TI AND TI’S LICENSORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL TI AND TI’S LICENSORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "tfl_main.h"

namespace tflite
{
  namespace main
  {

    void *in_ptrs[16] = {NULL};
    void *out_ptrs[16] = {NULL};

    /**
  *  \brief  Actual infernce happening 
  *  \param  ModelInfo YAML parsed model info
  *  \param  Settings user input options  and default values of setting if any
  * @returns void
  */
    void RunInference(tidl::modelInfo::ModelInfo *modelInfo, tidl::arg_parsing::Settings *s)
    {
      /* checking model path present or not*/
      if (!modelInfo->m_infConfig.modelFile.c_str())
      {
        LOG_ERROR("no model file name\n");
        exit(-1);
      }
      /* preparing tflite model  from file*/
      std::unique_ptr<tflite::FlatBufferModel> model;
      std::unique_ptr<tflite::Interpreter> interpreter;
      model = tflite::FlatBufferModel::BuildFromFile(modelInfo->m_infConfig.modelFile.c_str());
      if (!model)
      {
        LOG_ERROR("\nFailed to mmap model %s\n", modelInfo->m_infConfig.modelFile);
        exit(-1);
      }
      LOG_INFO("Loaded model %s \n", modelInfo->m_infConfig.modelFile.c_str());
      model->error_reporter();
      LOG_INFO("resolved reporter\n");

      tflite::ops::builtin::BuiltinOpResolver resolver;
      tflite::InterpreterBuilder(*model, resolver)(&interpreter);
      if (!interpreter)
      {
        LOG_ERROR("Failed to construct interpreter\n");
        exit(-1);
      }
      const std::vector<int> inputs = interpreter->inputs();
      const std::vector<int> outputs = interpreter->outputs();

      LOG_INFO("tensors size: %d \n", interpreter->tensors_size());
      LOG_INFO("nodes size: %d\n", interpreter->nodes_size());
      LOG_INFO("number of inputs: %d\n", inputs.size());
      LOG_INFO("number of outputs: %d\n", outputs.size());
      LOG_INFO("input(0) name: %s\n", interpreter->GetInputName(0));

      if (inputs.size() != 1)
      {
        LOG_ERROR("Supports only single input models \n");
        exit(-1);
      }

      if (s->log_level <= tidl::utils::DEBUG)
      {
        int t_size = interpreter->tensors_size();
        for (int i = 0; i < t_size; i++)
        {
          if (interpreter->tensor(i)->name)
            LOG_INFO("%d: %s,%d,%d,%d,%d\n", i, interpreter->tensor(i)->name,
                     interpreter->tensor(i)->bytes,
                     interpreter->tensor(i)->type,
                     interpreter->tensor(i)->params.scale,
                     interpreter->tensor(i)->params.zero_point);
        }
      }

      if (s->number_of_threads != -1)
      {
        interpreter->SetNumThreads(s->number_of_threads);
      }

      int input = inputs[0];
      if (s->log_level <= tidl::utils::INFO)
        LOG_INFO("input: %d\n", input);

      if (s->accel == 1)
      {
        /* This part creates the dlg_ptr */
        LOG_INFO("accelerated mode\n");
        typedef TfLiteDelegate *(*tflite_plugin_create_delegate)(char **, char **, size_t, void (*report_error)(const char *));
        tflite_plugin_create_delegate tflite_plugin_dlg_create;
        char *keys[] = {"artifacts_folder", "num_tidl_subgraphs", "debug_level"};
        char *values[] = {(char *)modelInfo->m_infConfig.artifactsPath.c_str(), "16", "0"};
        void *lib = dlopen("libtidl_tfl_delegate.so", RTLD_NOW);
        assert(lib);
        tflite_plugin_dlg_create = (tflite_plugin_create_delegate)dlsym(lib, "tflite_plugin_create_delegate");
        TfLiteDelegate *dlg_ptr = tflite_plugin_dlg_create(keys, values, 3, NULL);
        interpreter->ModifyGraphWithDelegate(dlg_ptr);
        LOG_INFO("ModifyGraphWithDelegate - Done \n");
      }
      if (interpreter->AllocateTensors() != kTfLiteOk)
      {
        LOG_ERROR("Failed to allocate tensors!");
        exit(-1);
      }

      if (s->device_mem)
      {
        LOG_INFO("device mem enabled\n");
        for (uint32_t i = 0; i < inputs.size(); i++)
        {
          const TfLiteTensor *tensor = interpreter->input_tensor(i);
          in_ptrs[i] = TIDLRT_allocSharedMem(tflite::kDefaultTensorAlignment, tensor->bytes);
          if (in_ptrs[i] == NULL)
          {
            LOG_INFO("Could not allocate Memory for input: %s\n", tensor->name);
          }
          interpreter->SetCustomAllocationForTensor(inputs[i], {in_ptrs[i], tensor->bytes});
        }
        for (uint32_t i = 0; i < outputs.size(); i++)
        {
          const TfLiteTensor *tensor = interpreter->output_tensor(i);
          out_ptrs[i] = TIDLRT_allocSharedMem(tflite::kDefaultTensorAlignment, tensor->bytes);
          if (out_ptrs[i] == NULL)
          {
            LOG_INFO("Could not allocate Memory for ouput: %s\n", tensor->name);
          }
          interpreter->SetCustomAllocationForTensor(outputs[i], {out_ptrs[i], tensor->bytes});
        }
      }

      if (s->log_level <= tidl::utils::DEBUG)
        PrintInterpreterState(interpreter.get());
      /* get input dimension from the YAML parsed  and batch 
      from input tensor assuming one tensor*/
      TfLiteIntArray *dims = interpreter->tensor(input)->dims;
      int wanted_batch = dims->data[0];
      int wanted_height = modelInfo->m_preProcCfg.outDataHeight;
      int wanted_width = modelInfo->m_preProcCfg.outDataWidth;
      int wanted_channels = modelInfo->m_preProcCfg.numChans;
      /* assuming NHWC*/
      if (wanted_channels != dims->data[3])
      {
        LOG_INFO("missmatch in YAML parsed wanted channels:%d and model channels:%d\n", wanted_channels, dims->data[3]);
      }
      if (wanted_height != dims->data[1])
      {
        LOG_INFO("missmatch in YAML parsed wanted height:%d and model height:%d\n", wanted_height, dims->data[1]);
      }
      if (wanted_width != dims->data[2])
      {
        LOG_INFO("missmatch in YAML parsed wanted width:%d and model width:%d\n", wanted_width, dims->data[2]);
      }
      cv::Mat img;
      switch (interpreter->tensor(input)->type)
      {
      case kTfLiteFloat32:
      {
        img = tidl::preprocess::preprocImage<float>(s->input_bmp_path, &interpreter->typed_tensor<float>(input)[0], modelInfo->m_preProcCfg);
        break;
      }
      case kTfLiteUInt8:
      {
        /* if model is already quantized update the scale and mean for preperocess computation */
        std::vector<float> temp_scale = modelInfo->m_preProcCfg.scale,
                           temp_mean = modelInfo->m_preProcCfg.mean;
        modelInfo->m_preProcCfg.scale = {1, 1, 1};
        modelInfo->m_preProcCfg.mean = {0, 0, 0};
        img = tidl::preprocess::preprocImage<uint8_t>(s->input_bmp_path, &interpreter->typed_tensor<uint8_t>(input)[0], modelInfo->m_preProcCfg);
        /*restoring mean and scale to preserve the data */
        modelInfo->m_preProcCfg.scale = temp_scale;
        modelInfo->m_preProcCfg.mean = temp_mean;
        break;
      }
      default:
        LOG_ERROR("cannot handle input type %d yet\n", interpreter->tensor(input)->type);
        exit(-1);
      }

      LOG_INFO("interpreter->Invoke - Started \n");
      if (s->loop_count > 1)
        for (int i = 0; i < s->number_of_warmup_runs; i++)
        {
          if (interpreter->Invoke() != kTfLiteOk)
          {
            LOG_ERROR("Failed to invoke tflite!\n");
          }
        }

      struct timeval start_time, stop_time;
      gettimeofday(&start_time, nullptr);
      for (int i = 0; i < s->loop_count; i++)
      {
        if (interpreter->Invoke() != kTfLiteOk)
        {
          LOG_ERROR("Failed to invoke tflite!\n");
        }
      }
      gettimeofday(&stop_time, nullptr);
      LOG_INFO("interpreter->Invoke - Done \n");

      LOG_INFO("average time:%f ms\n",
               (tidl::utility_functs::get_us(stop_time) - tidl::utility_functs::get_us(start_time)) / (s->loop_count * 1000));

      if (modelInfo->m_preProcCfg.taskType == "segmentation")
      {
        LOG_INFO("preparing segmentation result \n");
        TfLiteType type = interpreter->tensor(outputs[0])->type;
        float alpha = modelInfo->m_postProcCfg.alpha;
        if (type == TfLiteType::kTfLiteInt32)
        {
          int32_t *outputTensor = interpreter->tensor(outputs[0])->data.i32;
          img.data = tidl::postprocess::blendSegMask<int32_t>(img.data, outputTensor, img.cols, img.rows, wanted_width, wanted_height, alpha);
        }
        else if (type == TfLiteType::kTfLiteInt64)
        {
          int64_t *outputTensor = interpreter->tensor(outputs[0])->data.i64;
          img.data = tidl::postprocess::blendSegMask<int64_t>(img.data, outputTensor, img.cols, img.rows, wanted_width, wanted_height, alpha);
        }
        else if (type == TfLiteType::kTfLiteFloat32)
        {
          float *outputTensor = interpreter->tensor(outputs[0])->data.f;
          img.data = tidl::postprocess::blendSegMask<float>(img.data, outputTensor, img.cols, img.rows, wanted_width, wanted_height, alpha);
        }
      }
      else if (modelInfo->m_preProcCfg.taskType == "detection")
      {
        LOG_INFO("preparing detection result \n");
        std::vector<int32_t> format = {1, 0, 3, 2, 4, 5};
        float threshold = modelInfo->m_vizThreshold;
        if (tidl::utility_functs::is_same_format(format, modelInfo->m_postProcCfg.formatter))
        {
          float *detectection_location = interpreter->tensor(outputs[0])->data.f;
          float *detectection_classes = interpreter->tensor(outputs[1])->data.f;
          float *detectection_scores = interpreter->tensor(outputs[2])->data.f;
          int num_detections = (int)*interpreter->tensor(outputs[3])->data.f;
          LOG_INFO("results %d\n", num_detections);
          tidl::postprocess::overlayBoundingBox(img, num_detections, detectection_location, detectection_scores, threshold);
          for (int i = 0; i < num_detections; i++)
          {
            if (detectection_scores[i] > threshold)
            {
              LOG_INFO("class %f\n", detectection_classes[i]);
              LOG_INFO("cordinates %f %f %f %f\n", detectection_location[i * 4], detectection_location[i * 4 + 1], detectection_location[i * 4 + 2], detectection_location[i * 4 + 3]);
              LOG_INFO("score %f\n", detectection_scores[i]);
            }
          }
        }
        else
        {
          float *out_tensor = interpreter->tensor(outputs[0])->data.f;
          std::vector<float> detectection_classes, detectection_location, detectection_scores;
          int num_detections = 0;
          TfLiteIntArray *output_dims = interpreter->tensor(outputs[0])->dims;
          /*asssuming result is of format [1 x num_res x res_dim] */
          int res_dim = output_dims->data[output_dims->size - 1];
          int num_res = output_dims->data[output_dims->size - 2];
          for (int i = 0; i < num_res; i++)
          {
            float score = out_tensor[i * res_dim + res_dim - 2];
            float class_val = out_tensor[i * res_dim + res_dim - 1];
            /*TODO need to verify */
            float loc_0 = out_tensor[i * res_dim + 1] / modelInfo->m_postProcCfg.inDataHeight;
            float loc_1 = out_tensor[i * res_dim + 2] / modelInfo->m_postProcCfg.inDataHeight;
            float loc_2 = out_tensor[i * res_dim + 3] / modelInfo->m_postProcCfg.inDataHeight;
            float loc_3 = out_tensor[i * res_dim + 4] / modelInfo->m_postProcCfg.inDataHeight;
            if (score > threshold)
            {
              num_detections++;
              detectection_scores.push_back(score);
              detectection_classes.push_back(class_val);
              detectection_location.push_back(loc_0);
              detectection_location.push_back(loc_1);
              detectection_location.push_back(loc_2);
              detectection_location.push_back(loc_3);
            }
          }
          LOG_INFO("results %d\n", num_detections);
          for (int i = 0; i < num_detections; i++)
          {
            if (detectection_scores[i] > threshold)
            {
              LOG_INFO("class %f\n", detectection_classes[i]);
              LOG_INFO("cordinates %f %f %f %f\n", detectection_location[i * 4], detectection_location[i * 4 + 1], detectection_location[i * 4 + 2], detectection_location[i * 4 + 3]);
              LOG_INFO("score %f\n", detectection_scores[i]);
            }
          }
          tidl::postprocess::overlayBoundingBox(img, num_detections, detectection_location.data(), detectection_scores.data(), threshold);
        }
      }
      else if (modelInfo->m_preProcCfg.taskType == "classification")
      {
        LOG_INFO("preparing clasification result \n");
        const float threshold = 0.001f;
        std::vector<std::pair<float, int>> top_results;

        TfLiteIntArray *output_dims = interpreter->tensor(outputs[0])->dims;
        /* assume output dims to be something like (1, 1, ... ,size) */
        auto output_size = output_dims->data[output_dims->size - 1];
        int outputoffset;
        if (output_size == 1001)
          outputoffset = 0;
        else
          outputoffset = 1;
        switch (interpreter->tensor(outputs[0])->type)
        {
        case kTfLiteFloat32:
          tidl::postprocess::get_top_n<float>(interpreter->typed_output_tensor<float>(0), output_size,
                                              s->number_of_results, threshold, &top_results, true);
          break;
        case kTfLiteUInt8:
          tidl::postprocess::get_top_n<uint8_t>(interpreter->typed_output_tensor<uint8_t>(0),
                                                output_size, s->number_of_results, threshold,
                                                &top_results, false);
          break;
        default:
          LOG_ERROR("cannot handle output type %d yet", interpreter->tensor(outputs[0])->type);
          exit(-1);
        }

        std::vector<string> labels;
        size_t label_count;

        if (tidl::postprocess::ReadLabelsFile(s->labels_file_path, &labels, &label_count) != 0)
        {
          LOG_ERROR("label file not found!!! \n");
          exit(-1);
        }

        for (const auto &result : top_results)
        {
          const float confidence = result.first;
          const int index = result.second;
          LOG_INFO("%f: %d :%s\n", confidence, index, labels[index + outputoffset].c_str());
        }
        int num_results = 5;
        img.data = tidl::postprocess::overlayTopNClasses(img.data, top_results, &labels, img.cols, img.rows, num_results);
      }
      LOG_INFO("saving image result file \n");
      cv::cvtColor(img, img, cv::COLOR_RGB2BGR);
      char filename[500];
      strcpy(filename, "test_data/");
      strcat(filename, "cpp_inference_out");
      strcat(filename, modelInfo->m_preProcCfg.modelName.c_str());
      strcat(filename, ".jpg");
      bool check = cv::imwrite(filename, img);
      if (check == false)
      {
        LOG_INFO("Saving the image, FAILED\n");
      }

      if (s->device_mem)
      {
        for (uint32_t i = 0; i < inputs.size(); i++)
        {
          if (in_ptrs[i])
          {
            TIDLRT_freeSharedMem(in_ptrs[i]);
          }
        }
        for (uint32_t i = 0; i < outputs.size(); i++)
        {
          if (out_ptrs[i])
          {
            TIDLRT_freeSharedMem(out_ptrs[i]);
          }
        }
      }
    }

  } // namespace main
} // namespace tflite

int main(int argc, char **argv)
{
  tidl::arg_parsing::Settings s;
  tidl::arg_parsing::parse_args(argc, argv, &s);
  tidl::arg_parsing::dump_args(&s);
  tidl::utils::logSetLevel((tidl::utils::LogLevel)s.log_level);
  /* Parse the input configuration file */
  tidl::modelInfo::ModelInfo model(s.model_zoo_path);

  int status = model.initialize();
  tflite::main::RunInference(&model, &s);

  return 0;
}
