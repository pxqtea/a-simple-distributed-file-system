package main

/* this part is written by project collaborator Rex Fernando */

import (
	pb "~/cs739/p2/proto"
	"fmt"
	"net"
        "log"
        "os"
        "path"
        "io/ioutil"

	"golang.org/x/net/context"
	"google.golang.org/grpc"
)

const port = 7890



type fileSystemServer struct {
    cachePath string
}

func (s *fileSystemServer) adjustPath(relPath string) (string) {
    return path.Join(s.cachePath, relPath)
}

// directory name -> success
func (s *fileSystemServer) MkDir(c context.Context, p *pb.Path) (*pb.Success, error) {
    err := os.Mkdir(s.adjustPath(p.Path), 0777)
    if err != nil {
        err := err.(*os.PathError)
        err.Path = p.Path
        return &pb.Success{false}, err
    }

    return &pb.Success{true}, nil
}

// directory name -> success
func (s *fileSystemServer) RmDir(c context.Context, p *pb.Path) (*pb.Success, error) {
    err := os.Remove(s.adjustPath(p.Path))
    if err != nil {
        err := err.(*os.PathError)
        err.Path = p.Path
        return &pb.Success{false}, err
    }

    return &pb.Success{true}, nil
}

// directory name -> directory listing
func (s *fileSystemServer) ReadDir(c context.Context, p *pb.Path) (*pb.DirListing, error) {
    f, err := os.Open(s.adjustPath(p.Path))
    if err != nil {
        return &pb.DirListing{make([]string, 0)}, err
    }
    defer f.Close()

    names, err := f.Readdirnames(0)
    if err != nil {
        return &pb.DirListing{make([]string, 0)}, err
    }

    return &pb.DirListing{names}, nil
}

func (s *fileSystemServer) Stat(c context.Context, p *pb.Path) (*pb.FileInfo, error) {
    info, err := os.Stat(s.adjustPath(p.Path))
    if err != nil {
        info, err = os.Stat(path.Dir(s.adjustPath(p.Path)))
        if err != nil {
            return &pb.FileInfo{}, err
        }

        return &pb.FileInfo{"", 0, 0, false, true}, nil
    }

    return &pb.FileInfo{info.Name(), info.Size(), 
                        info.ModTime().Unix(), info.IsDir(), 
                        false},nil
}

// old filename, new filename -> success
func (s *fileSystemServer) Rename(c context.Context, p *pb.RenameParameters) (*pb.Success, error) {
    err := os.Rename(s.adjustPath(p.Old), s.adjustPath(p.New))
    if err != nil {
        err := err.(*os.LinkError)
        err.Old = p.Old
        err.New = p.New
        return &pb.Success{false}, err
    }

    return &pb.Success{true}, nil
}

// filename -> success
func (s *fileSystemServer) Unlink(c context.Context, p *pb.Path) (*pb.Success, error) {
    err := os.Remove(s.adjustPath(p.Path))
    if err != nil {
        err := err.(*os.PathError)
        err.Path = p.Path
        return &pb.Success{false}, err
    }

    return &pb.Success{true}, nil
}

// filename -> contents
func (s *fileSystemServer) Get(c context.Context, p *pb.Path) (*pb.Contents, error) {
    contents, err := ioutil.ReadFile(s.adjustPath(p.Path))
    if err != nil {
        return &pb.Contents{make([]byte, 0)}, err
    }

    return &pb.Contents{contents}, nil
}

// filename, contents -> success
func (s *fileSystemServer) Put(c context.Context, p *pb.PutParameters) (*pb.Success, error) {
    tempFile, err := ioutil.TempFile("tmp", "fs739");
    if err != nil {
        fmt.Println("Error: server temp file not found")
        return &pb.Success{false}, err
    }
    _, err = tempFile.Write(p.Contents)
    if err != nil {
        fmt.Println("b")
        return &pb.Success{false}, err
    }
    err = tempFile.Sync()
    if err != nil {
        fmt.Println("c")
        return &pb.Success{false}, err
    }
    err = tempFile.Chmod(0777)
    if err != nil {
        fmt.Println(err)
        return &pb.Success{false}, err
    }

    tempFileName := tempFile.Name()

    err = tempFile.Close()
    if err != nil {
        fmt.Println("d")
        return &pb.Success{false}, err
    }
    err = os.Rename(tempFileName, s.adjustPath(p.Path))
    if err != nil {
        fmt.Println(err)
        return &pb.Success{false}, err
    }

    return &pb.Success{true}, nil

}




func main() {
    if len(os.Args) != 2 {
        fmt.Fprintln(os.Stderr, "Usage: server <path-to-cache>")
        os.Exit(1)
    }

    lis, err := net.Listen("tcp", fmt.Sprintf(":%d", port))
    if err != nil {
        log.Fatalf("failed to listen: %v", err)
    }
    grpcServer := grpc.NewServer()
    pb.RegisterFileSystemServer(grpcServer, &fileSystemServer{cachePath: os.Args[1]})
    grpcServer.Serve(lis)
}
