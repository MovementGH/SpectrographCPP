#include "Spectrogram.hpp"
SpectrographGen::SpectrographGen(int SpecSampleRate,int SpecResPerSample,float Compression): m_Samples(),m_SampleRate(0),m_SpecSampleRate(SpecSampleRate),m_Compression(Compression),FFTBase(SpecResPerSample){}
void SpectrographGen::generateLine(sf::Uint8* Pixels,int LineStart,int LineEnd,sf::Vector2i& Size) {
    std::vector<std::complex<float>> Temp(m_FFTWidth/2);
    std::vector<std::complex<float>> WorkingSamples(m_FFTWidth,{0,0});
    sf::Uint32 res=0,pstr=WorkingSamples.size()*(.5-1/m_Compression),index=0;
    for(int i=LineStart;i<LineEnd;i++) {
        std::size_t start=std::min((std::size_t)i*(m_SampleRate/m_SpecSampleRate),m_Samples.size()-m_FFTWidth-1);
        for(std::size_t i2=start;i2<start+m_FFTWidth;i2++)
            WorkingSamples[i2-start].real(m_Samples[i2]*m_Hanning[i2-start]);
        fft(Temp.data(),WorkingSamples.data(),(int)WorkingSamples.size(),m_FFTSize);
        for(int i2=pstr;i2<WorkingSamples.size()/2;i2++)
            res=std::min(std::max(WorkingSamples[i2+WorkingSamples.size()/2].real()/20.0f+8388608.f,0.f),16777216.f),
            index=i+(i2-pstr)*Size.x,
            Pixels[index]=(sf::Uint8)(res%256),
            Pixels[index+1]=(sf::Uint8)((res-Pixels[index])%65536),
            Pixels[index+2]=(sf::Uint8)((res-Pixels[index+1])%16777216),
            Pixels[index+3]=255;
    }
}
sf::Image SpectrographGen::generateImage() {
    sf::Vector2i Size(m_SpecSampleRate*((int)m_Samples.size()/m_SampleRate),m_FFTWidth/m_Compression);
    sf::Uint8* Pixels=new sf::Uint8[Size.x*Size.y*4];
    std::vector<std::thread*> Threads(std::thread::hardware_concurrency());
    for(int i=0;i<Threads.size();i++)
        Threads[i]=new std::thread([&]{generateLine(Pixels,(float)Size.x*((float)i/(float)Threads.size()),(float)Size.x*((float)(i+1)/(float)Threads.size()),Size);});
    for(int i=0;i<Threads.size();i++)
        Threads[i]->join(),
        delete Threads[i];
    sf::Image Result;
    Result.create(Size.x,Size.y,Pixels);
    delete[] Pixels;
    return Result;
}
void SpectrographGen::saveToFile(std::string File) {
    generateImage().saveToFile(File);
}
bool SpectrographGen::loadFromFile(std::string File) {
    sf::SoundBuffer Buffer;
    if(!Buffer.loadFromFile(File))
        return false;
    loadFromBuffer(Buffer);
    return true;
}
void SpectrographGen::loadFromBuffer(sf::SoundBuffer& Buffer) {
    m_SampleRate=Buffer.getSampleRate();
    m_Samples.resize(Buffer.getSampleCount());
    for(int i=0;i<Buffer.getSampleCount();i++)
        m_Samples[i]=Buffer.getSamples()[i];
}
SpectrographDecode::SpectrographDecode(int SpecSampleRate,int SampleRate,int ChannelCount,float Compression):m_SpecSampleRate(SpecSampleRate),m_SampleRate(SampleRate), m_ChannelCount(ChannelCount),m_Compression(Compression),FFTBase(1){}
void SpectrographDecode::decodeLine(sf::Int16* Output,int StartLine,int EndLine) {
    std::vector<std::complex<float>> WorkingSamples(m_FFTWidth,std::complex<float>(0,0));
    std::vector<std::complex<float>> Temp(m_FFTWidth/2);
    const sf::Uint8* Pixel=m_Image.getPixelsPtr();
    float Squishing=(((float)m_SampleRate/(float)m_FFTWidth)/(float)m_SpecSampleRate)/250.f;
    int HalfSize=WorkingSamples.size()/m_Compression,FrameWidth=m_SampleRate/m_SpecSampleRate;
    sf::Int32 Index;
    for(int x=StartLine;x<EndLine;x++) {
        for(int i=0;i<HalfSize;i++)
            Index=(x+(m_Image.getSize().y-i-1)*m_Image.getSize().x)*4,
            WorkingSamples[HalfSize+i].real((Pixel[Index+2]*65536+Pixel[Index+1]*256+Pixel[Index])-8388608),
            WorkingSamples[HalfSize/2-i].real(WorkingSamples[HalfSize+i].real());
        ifft(Temp.data(),WorkingSamples,m_FFTSize);
        for(int i2=0;i2<WorkingSamples.size();i2++)
            Output[x*FrameWidth+i2]+=WorkingSamples[i2].real()*m_Hanning[i2]*m_Hanning[i2]*Squishing;
    }
}
sf::SoundBuffer SpectrographDecode::generateBuffer() {
    sf::Int16* Samples=new sf::Int16[((float)m_Image.getSize().x/(float)m_SpecSampleRate)*m_SampleRate+m_FFTWidth];
    sf::Vector2u Size=m_Image.getSize();
    std::vector<std::thread*> Threads(std::thread::hardware_concurrency());
    for(int i=0;i<Threads.size();i++)
        Threads[i]=new std::thread(&SpectrographDecode::decodeLine,this,Samples,(float)Size.x*((float)i/(float)Threads.size()),(float)Size.x*((float)(i+1)/(float)Threads.size()));
    for(int i=0;i<Threads.size();i++)
        Threads[i]->join();
    for(int i=0;i<Threads.size();i++)
        delete Threads[i];
    sf::SoundBuffer Buffer;
    Buffer.loadFromSamples(Samples,((float)m_Image.getSize().x/(float)m_SpecSampleRate)*m_SampleRate+m_FFTWidth,m_ChannelCount,m_SampleRate);
    delete[] Samples;
    return Buffer;
}
void SpectrographDecode::saveToFile(std::string File) {
    generateBuffer().saveToFile(File);
}
void SpectrographDecode::loadFromImage(sf::Image& Image) {
    m_Image=Image;
    init(m_Image.getSize().y*m_Compression);
}
bool SpectrographDecode::loadFromFile(std::string File) {
    if(!m_Image.loadFromFile(File))
        return false;
    init(m_Image.getSize().y*m_Compression);
    return true;
}
FFTBase::FFTBase(int FFTWidth) {
    init(FFTWidth);
}
void FFTBase::init(int FFTWidth) {
    m_FFTWidth=FFTWidth;
    m_Hanning.resize(m_FFTWidth);
    for(int i(0);i<m_FFTWidth;i++)
        m_Hanning[i]=0.54-0.46*cos(2*M_PI*i/(float)m_FFTWidth);
    m_FFTSize=log2(m_FFTWidth);
    m_Polar.resize(m_FFTSize,std::vector<std::complex<float>>(m_FFTWidth/2));
    for(int i=0;i<m_FFTSize;i++)
        for(int i2=0;i2<m_FFTWidth/2;i2++)
            m_Polar[i][i2]=(exp(std::complex<float>(0,-2*M_PI/(pow(2,i+1))*i2)));
}
void FFTBase::sort(std::complex<float>* tmp,std::complex<float>* a,int n) {
    for(int i=0;i<n;i++) tmp[i]=a[i*2+1];
    for(int i=0;i<n;i++) a[i]=a[i*2];
    for(int i=0;i<n;i++) a[i+n]=tmp[i];
}
void FFTBase::fft(std::complex<float>* tmp,std::complex<float>* x,int n,int s) {
    std::complex<float> t;
    if(n>1) {
        n/=2,sort(tmp,&x[0],n),fft(tmp,&x[0],n,s-1),fft(tmp,&x[0]+n,n,s-1);
        for(int k=0;k<n;k++)
            t=m_Polar[s-1][k]*x[k+n],x[k+n]=x[k]-t,x[k]+=t;
    }
}
void FFTBase::ifft(std::complex<float>* tmp,std::vector<std::complex<float>>& x,int s) {
    std::transform(x.cbegin(),x.cend(),x.begin(),static_cast<std::complex<double>(*)(const std::complex<double>&)>(std::conj));
    fft(tmp,x.data(),(int)x.size(),s);
    std::transform(x.cbegin(),x.cend(),x.begin(),static_cast<std::complex<double>(*)(const std::complex<double>&)>(std::conj));
}
