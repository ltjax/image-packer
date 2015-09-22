
#define REPLAY_USE_LIBPNG

#include <replay/pixbuf.hpp>
#include <replay/pixbuf_io.hpp>
#include <replay/box_packer.hpp>
#include <replay/vector2.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/optional.hpp>
#include <replay/pixbuf_io.hpp>
#include <tuple>

struct ImageEntryType
{
    ImageEntryType() : IsNinePatch(false) {}
    
    boost::filesystem::path	RelativePath;
    replay::shared_pixbuf	Image;
    replay::box<int>		Box;
    
    bool					IsNinePatch;
    
    replay::box<int>		ScaleableArea;
    replay::box<int>		FillArea;
};

std::tuple<unsigned int, unsigned int>
AnalyzeLine(replay::pixbuf const& Image, unsigned int Offset, unsigned int Axis)
{
    // This needs RGBA
    assert(Image.get_channels()==4);

    replay::vector2i Coord(0);
    Coord[Axis^1]=Offset;

    replay::vector2i Delta(0);
    Delta[Axis]=1;

    // Alias the size of the image for brevity
    auto const w=static_cast<int>(Image.get_width());
    auto const h=static_cast<int>(Image.get_height());

    // Move to the beginning
    while (Coord[0]<w && Coord[1]<h && Image.get_pixel(Coord[0], Coord[1])[3]==0)
        Coord+=Delta;

    // Mark the beginning of the black line
    unsigned int Begin=Coord[Axis];

    unsigned char Black[]={0,0,0,255};
    while (Coord[0]<w && Coord[1]<h && std::equal(Black, Black+4, Image.get_pixel(Coord[0], Coord[1])))
        Coord+=Delta;

    // Mark the end of the black line
    unsigned int End=Coord[Axis];

    while (Coord[0]<w && Coord[1]<h && Image.get_pixel(Coord[0], Coord[1])[3]==0)
        Coord+=Delta;

    // Check if we reached the end
    if (Coord[0]!=w && Coord[1]!=h)
        throw std::invalid_argument("Invalid black-line size specifier.");

    // Subtract 1 because of the added border (results need to be relative to the cropped image)
    return std::make_tuple(Begin-1, End-1);
}

void AddFile(std::vector<ImageEntryType>& List, boost::filesystem::path const& FilePath, boost::filesystem::path const& RelativeFilePath)
{
    // Check if this is a supported image format
    if (extension(FilePath)!=".png")
        return;

    std::cout << "Loading " << FilePath.string() << std::endl;
        
    auto Image=replay::pixbuf_io::load_from_file(FilePath);
    ImageEntryType Entry;
    Entry.RelativePath=RelativeFilePath;

    // Check if this is a 9-patch
    if (boost::algorithm::ends_with(FilePath.leaf().string(), ".9.png"))
    {
        auto w=Image->get_width(), h=Image->get_height();
        if (Image->get_channels() != 4)
            throw std::invalid_argument("9-patch must have alpha channel");

        if (w < 4 || h < 4)
            throw std::invalid_argument("9-patch images must be at least 4x4");

        // Analyze top and left
        auto ScalableX=AnalyzeLine(*Image, h-1, 0);
        auto ScalableY=AnalyzeLine(*Image, 0, 1);
        
        // Analyze bottom and right
        auto FillX=AnalyzeLine(*Image, 0, 0);
        auto FillY=AnalyzeLine(*Image, w-1, 1);

        if (std::get<0>(FillX)==std::get<1>(FillX))
            FillX=ScalableX;

        if (std::get<0>(FillY)==std::get<1>(FillY))
            FillY=ScalableY;
        
        Entry.ScaleableArea.set(std::get<0>(ScalableX), std::get<0>(ScalableY),
								std::get<1>(ScalableX), std::get<1>(ScalableY));

        Entry.FillArea.set(std::get<0>(FillX), std::get<0>(FillY),
						   std::get<1>(FillX), std::get<1>(FillY));

        // Extract the actual image data
        Entry.Image=Image->get_sub_image(1, 1, w-2, h-2);

        Entry.IsNinePatch=true;
    }
    else
    {
        Entry.Image=Image;
    }
        
        
    List.push_back(Entry);
}

void ScanFile(std::vector<ImageEntryType>& List, boost::filesystem::path const& Path)
{
    if (!is_directory(Path))
    {
        AddFile(List, Path, Path.leaf());
        return;
    }
    
    using namespace boost::filesystem;
    typedef boost::filesystem::recursive_directory_iterator Iterator;
    std::size_t BaseOffset=std::distance(Path.begin(), Path.end());
    for (Iterator i(Path), ie; i!=ie; ++i)
    {
        if (is_symlink(*i))
            i.no_push();
        
        if (!is_regular_file(*i))
            continue;
        
        path Absolute=*i;
        path Relative;
        auto j=Absolute.begin();
        
        std::size_t c=0;
        for (; j!=Absolute.end(); ++j, ++c)
            if (c >= BaseOffset)
                Relative /= *j;
        
        if (!Relative.empty())
            AddFile(List, Path/Relative, Relative);
    }
}

bool PackInto(std::vector<ImageEntryType>& List, int Width, int Height)
{
    replay::box_packer Packer(Width, Height);
    
    for (auto i=begin(List); i!=end(List); ++i)
        if (!Packer.pack(i->Image->get_width(), i->Image->get_height(), &i->Box))
            return false;
    
    return true;
}

void BlitImages(replay::pixbuf& Result, std::vector<ImageEntryType> const& List)
{
    for (auto&& Entry : List)
    {
        Entry.Image->convert_to_rgba();
        
        if (!Result.blit(Entry.Box.left, Entry.Box.bottom, *Entry.Image))
            throw std::runtime_error("Unable to blit image" + Entry.RelativePath.string());
    }
}

void PackImages(boost::filesystem::path const& ResultImage, std::vector<ImageEntryType>& List)
{
    // Start by trying to open the result file
    boost::filesystem::ofstream File(ResultImage, std::ios::binary|std::ios::trunc);
    
    if (!File.good())
        throw std::runtime_error("Unable to open target file: "+ResultImage.string());
    
    // Use an appropriate starting size
    int PixelCount=0;
    int MinWidth=0;
    int MinHeight=0;

    for (auto&& Entry : List)
    {
        auto&& Image(*Entry.Image);
        MinWidth=std::max<int>(MinWidth, Image.get_width());
        MinHeight=std::max<int>(MinHeight, Image.get_height());
        PixelCount+=Image.get_width()*Image.get_height();
    }
    
	// Start with a (very) rough estimation of the size
    int Width=128, Height=128;

    while (Width<MinWidth) Width *= 2;
    while (Height<MinHeight) Height *= 2;
    
	// Progressively scale up until we can pack into the image
    while (Width*Height < PixelCount || 
		   !PackInto(List, Width, Height))
    {
        if (Width <= Height)
            Width *= 2;
        else
            Height *= 2;
    }
    
    auto Result=replay::pixbuf::create(Width, Height, replay::pixbuf::rgba);
    Result->fill(0,0,0,0);
    
    BlitImages(*Result, List);
    
    replay::pixbuf_io::save_to_png_file(File, *Result, 9);
}

void WriteBox(std::ofstream& File, replay::box<int> const& Box)
{
    File << "x=" << Box.left <<
        ", y=" << Box.bottom <<
        ", w=" << Box.get_width() <<
        ", h=" << Box.get_height();
}

void WriteDictionary(std::ofstream& File, const std::string& ModuleName, const std::string& DictionaryName, const std::string& NinePatchDir, std::vector<ImageEntryType> const& List)
{
    File << "local "<<ModuleName<<"={}\n\n";

    File << "-- Dictionary for regular images\n";
    File << ModuleName << "." << DictionaryName << "={\n";

    std::size_t NinePatchCount=0;
    
    for (auto i=List.begin(); i!=List.end(); ++i)
    {
        auto&& Box=i->Box;

        if (i->IsNinePatch)
        {
            ++NinePatchCount;
            continue;
        }

        // Remove the file extension
        boost::filesystem::path NameOnly=i->RelativePath;
        NameOnly.replace_extension();

        File << "\t[\"" << NameOnly.string();
        File << "\"]={Box={";
        WriteBox(File, Box);
        File << "}}";
        
        if (i+1 != List.end())
            File << ",\n";
        else
            File << "\n";
    }
    File << "}\n\n";
    
    if (NinePatchCount)
    {
        File<<"-- Dictionary for 9patch images\n";
        File<<ModuleName<<"."<<NinePatchDir<<"={\n";
        for (auto i=List.begin(); i!=List.end(); ++i)
        {
            auto&& Box=i->Box;

            if (!i->IsNinePatch)
                continue;

            // Remove the file extension
            boost::filesystem::path NameOnly=i->RelativePath;
            NameOnly.replace_extension();
            NameOnly.replace_extension(); // twice for the ".9"
        

            File << "\t[\"" << NameOnly.string();
            File << "\"]={Box={";
            WriteBox(File, Box);
            File << "}, Scalable={";
            WriteBox(File, i->ScaleableArea);
            File << "}, Fill={";
            WriteBox(File, i->FillArea);
            File << "}}";
        
            if (i+1 != List.end())
                File << ",\n";
            else
                File << "\n";
        }
        File << "}\n\n";
    }

    File<<"return "<<ModuleName<<std::endl;
}

void MakePackedImage(
    boost::filesystem::path const& ImagePath,
    boost::filesystem::path const& ScriptPath,
    std::string const& DictionaryName,
    std::vector<std::string> const& SourceList)
{
    
    std::vector<ImageEntryType> ImageList;
    
    boost::filesystem::ofstream DictionaryFile(ScriptPath, std::ios::trunc);
    
    for (auto i=begin(SourceList); i!=end(SourceList); ++i)
    {
        ScanFile(ImageList, *i);
    }

    // Construct the module name from the filename
    std::string ModuleName;
    {
        auto p=ScriptPath.leaf();
        p.replace_extension();
        ModuleName=p.string();
    }

    // Sort according to size (max edge length)
    std::sort(ImageList.begin(), ImageList.end(), [](ImageEntryType const& Lhs, ImageEntryType const& Rhs) ->bool {
        auto&& a=*Lhs.Image;
        auto&& b=*Rhs.Image;
        return std::max(a.get_width(), a.get_height()) > std::max(b.get_width(), b.get_height());
    });
    
    PackImages(ImagePath, ImageList);
    WriteDictionary(DictionaryFile, ModuleName, DictionaryName, "NinePatches", ImageList);
}

int main(int argc, char* argv[])
{
    namespace po = boost::program_options;
    po::options_description desc("Allowed options");
    
    
    std::string ImagePath, ScriptPath, DictionaryName;
    std::string Code;
    std::vector<std::string> SourceList;
    
    desc.add_options()
        ("image-path", po::value<std::string>(&ImagePath)->default_value("packed_image.png"), "Set the path where to write the packed image data")
        ("script-path", po::value<std::string>(&ScriptPath)->default_value("packed_image.lua"), "Set the path where to write the dictionary for the image data")
        ("dict-name", po::value<std::string>(&DictionaryName)->default_value("Images"), "Set the variable-name for the generated table")
        ("image", po::value<std::vector<std::string>>(&SourceList), "Individual paths or folders to use as sources")
    ;
    
    po::positional_options_description p;
    p.add("image", -1);
    
    po::variables_map VariableMap;
    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), VariableMap);
    
    po::notify(VariableMap);
    
    try {
        MakePackedImage(ImagePath, ScriptPath, DictionaryName, SourceList);
        
    } catch (std::exception const& Error)
    {
        std::cerr << Error.what();
    }
    
    return 0;
}
