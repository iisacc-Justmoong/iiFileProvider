#include <iiFileProviderStorage.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef IIFILEPROVIDERNATIVE_STATIC_DEFINE
#error Static provider consumers must inherit the static export definition
#endif
int main(int argc,char** argv){
 try{
  if(argc!=2)throw std::runtime_error("explicit root required");
  const auto root=std::filesystem::path(argv[1])/"static-provider";
  iiFileProvider::native::createDirectories(root/u8"한글");
  const auto path=root/u8"한글/contract.txt";
  {iiFileProvider::native::OutputFile file(path);file<<"native static";file.close();if(!file)throw std::runtime_error("write failed");}
  std::ifstream input(path);std::string value;std::getline(input,value);
  if(value!="native static")throw std::runtime_error("readback differs");
  input.close();std::filesystem::remove_all(root);return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
