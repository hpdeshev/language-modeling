# Demonstration of GPT-2 architecture model pretraining for C++ code completion

## Overview

The `cpp_code_completion.ipynb` notebook demonstrates how a  [GPT-2](https://huggingface.co/openai-community/gpt2) architecture model can be pretrained for C++ code completion, based on a single source file. The goal is to limit the required computing resources for training and evaluation. The demonstrated method utilizes the [Hugging Face Transformers](https://huggingface.co/docs/transformers/index) and [PyTorch](https://pytorch.org/get-started/locally) deep learning libraries, and more specifically - the [DistilGPT2](https://huggingface.co/distilbert/distilgpt2) language model without pretrained weights.

For simplicity reasons, for dataset is selected a C++ source file.
