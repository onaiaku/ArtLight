using ArtLightControlService;

var builder = Host.CreateApplicationBuilder(args);
builder.Services.AddWindowsService(options => options.ServiceName = "ArtLightControlService");
builder.Services.AddHostedService<PipeWorker>();

var host = builder.Build();
host.Run();
