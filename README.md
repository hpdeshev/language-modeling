# Demonstration of pretraining and finetuning for C++ code completion

## Overview

### Pretraining

The `cpp_code_completion_pretraining.ipynb` notebook demonstrates how a  [GPT-2](https://huggingface.co/openai-community/gpt2) architecture model can be pretrained for C++ code completion, based on a single source file. The goal is to limit the required computing resources for training and evaluation. The demonstrated method utilizes the [Hugging Face Transformers](https://huggingface.co/docs/transformers/index) and [PyTorch](https://pytorch.org/get-started/locally) deep learning libraries, and more specifically - the [DistilGPT2](https://huggingface.co/distilbert/distilgpt2) language model without pretrained weights.

### Finetuning

The `cpp_code_completion_finetuning.ipynb` notebook demonstrates how a [Granite-3B-Code-Base-2K](https://huggingface.co/ibm-granite/granite-3b-code-base-2k) model can be finetuned for C++ code completion, based on *.cc* and *.h* files in a folder. The goal is to limit the required computing resources through [quantization](https://huggingface.co/docs/transformers/quantization/overview) and [parameter-efficient fine-tuning (PEFT)](https://huggingface.co/docs/peft/index). The demonstrated method utilizes the [Hugging Face Transformers](https://huggingface.co/docs/transformers/index) and [PyTorch](https://pytorch.org/get-started/locally) deep learning libraries.
